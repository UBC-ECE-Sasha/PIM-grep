#define _DEFAULT_SOURCE // needed for S_ISREG() and strdup
#include <dpu.h>
#include <dpu_memory.h>
#include <dpu_log.h>
#include <dpu_management.h>
#include <dpu_runner.h>
#include <unistd.h>

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>

#include "grep-host.h"

#define DPU_PROGRAM "dpu-grep/grep.dpu"
#define MAX_INPUT_LENGTH MEGABYTE(4)
#define MAX_OUTPUT_LENGTH MEGABYTE(32)
#define MIN_CHUNK_SIZE 256 // not worthwhile making another tasklet work for data less than this
#define MAX_PATTERN 63
#define TEMP_LENGTH 256
#define MAX_RANKS 10

const char options[]="A:B:cdt:";
static char dummy_buffer[MAX_INPUT_LENGTH];
static uint32_t rank_count, dpu_count;
static uint32_t dpus_per_rank;

int search_rank(struct dpu_set_t dpu_rank, uint8_t rank_id, struct host_buffer_descriptor input[], 
	uint8_t count, const char* pattern, struct grep_options* opts)
{
	struct dpu_set_t dpu;
	uint32_t dpu_id=0; // the id of the DPU inside the rank (0-63)
	uint32_t pattern_length = strlen(pattern);
	UNUSED(rank_id);

	// copy the common items to all DPUs in the rank
	DPU_ASSERT(dpu_copy_to(dpu_rank, "pattern_length", 0, &pattern_length, sizeof(uint32_t)));
	DPU_ASSERT(dpu_copy_to(dpu_rank, "pattern", 0, pattern, ALIGN(pattern_length, 4)));
	DPU_ASSERT(dpu_copy_to(dpu_rank, "options", 0, opts, ALIGN(sizeof(struct grep_options), 8)));

	// copy the items specific to each DPU
#ifdef BULK_TRANSFER
	uint32_t largest_length=0; // largest file size we have to transfer
#endif // BULK_TRANSFER
	const char* buffer;
	DPU_FOREACH(dpu_rank, dpu, dpu_id)
	{
		uint32_t input_length;
		uint32_t chunk_size;

		if (dpu_id < count)
		{
			input_length = input[dpu_id].length;
			chunk_size = MAX(MIN_CHUNK_SIZE, ALIGN(input_length / NR_TASKLETS, 16));
			buffer = input[dpu_id].buffer;
			dbg_printf("%s (%u) to dpu %u\n", input[dpu_id].filename, input_length, dpu_id);
		}
		else
		{
			input_length = 0;
			chunk_size = 0;
			buffer = dummy_buffer;
		}

		// copy the data to the DPU
		DPU_ASSERT(dpu_copy_to(dpu, "dpu_id", 0, &dpu_id, sizeof(uint32_t)));
		DPU_ASSERT(dpu_copy_to(dpu, "input_chunk_size", 0, &chunk_size, sizeof(uint32_t)));
		DPU_ASSERT(dpu_copy_to(dpu, "input_length", 0, &input_length, sizeof(uint32_t)));
#ifdef BULK_TRANSFER
		DPU_ASSERT(dpu_prepare_xfer(dpu, (void*)buffer));
		if (input_length > largest_length)
			largest_length = input_length;
#else
		DPU_ASSERT(dpu_copy_to(dpu, "input_buffer", 0, buffer, ALIGN(input_length, 8)));
#endif //BULK_TRANSFER
	}

#ifdef BULK_TRANSFER
	DPU_ASSERT(dpu_push_xfer(dpu_rank, DPU_XFER_TO_DPU, "input_buffer", 0, ALIGN(largest_length, 8), DPU_XFER_DEFAULT));
#endif //BULK_TRANSFER

	// launch all of the DPUs after they have been loaded
	DPU_ASSERT(dpu_launch(dpu_rank, DPU_ASYNCHRONOUS));

	return 0;
}

int read_results_dpu_rank(struct dpu_set_t dpu_rank, struct host_context *output)
{
	struct dpu_set_t dpu;
	uint32_t line_count[NR_TASKLETS];
	uint32_t match_count[NR_TASKLETS];
	uint8_t dpu_id;

	DPU_FOREACH(dpu_rank, dpu, dpu_id)
	{
		if (dpu_id > output->used)
			break;

#ifdef DEBUG_DPU
		printf("Retrieving results from %s\n", output->desc[dpu_id].filename);
		DPU_ASSERT(dpu_log_read(dpu, stdout));
#endif // DEBUG_DPU

		// Get the results back from each individual DPU in the rank
		DPU_ASSERT(dpu_copy_from(dpu, "line_count", 0, line_count, sizeof(uint32_t) * NR_TASKLETS));
		DPU_ASSERT(dpu_copy_from(dpu, "match_count", 0, match_count, sizeof(uint32_t) * NR_TASKLETS));
		//DPU_ASSERT(dpu_copy_from(dpu, "matches", 0, matches, sizeof(uint32_t) * match_count);

		// aggregate the statistics
		for (uint8_t i=0; i < NR_TASKLETS; i++)
		{
			output->desc[dpu_id].line_count += line_count[i];
			output->desc[dpu_id].match_count += match_count[i];
		}
	}

	return 0;
}

int check_for_completed_rank(struct dpu_set_t dpus, uint64_t* rank_status, struct host_context ctx[], host_results *results)
{
	struct dpu_set_t dpu_rank;
	uint8_t rank_id=0;

	DPU_RANK_FOREACH(dpus, dpu_rank)
	{
		bool done, fault;

		if (*rank_status & ((uint64_t)1<<rank_id))
		{
			// check to see if anything has completed
			dpu_status(dpu_rank, &done, &fault);
			if (done)
			{
				uint32_t dpu_id;
				struct host_context* rank_ctx = &ctx[rank_id];
				*rank_status &= ~((uint64_t)1<<rank_id);
				dbg_printf("Reading results from rank %u descriptor %p\n", rank_id, rank_ctx->desc);
				read_results_dpu_rank(dpu_rank, rank_ctx);
				for (dpu_id=0; dpu_id < rank_ctx->used; dpu_id++)
				{
					printf("%s:%u\n", rank_ctx->desc[dpu_id].filename, rank_ctx->desc[dpu_id].match_count);
					results->total_line_count += rank_ctx->desc[dpu_id].line_count;
					results->total_match_count += rank_ctx->desc[dpu_id].match_count;
				}
			}
			if (fault)
			{
				printf("rank %u fault - abort!\n", rank_id);
				return -2;
			}
		}
		rank_id++;
	}
	return 0;
}

/**
 * Read the contents of a file into an in-memory buffer. Upon success,
 * return 0 and write the amount read to input->length.
 *
 * @param in_file The input filename.
 * @param input The struct to which contents of file are written to.
 */
static int read_input_host(char *in_file, struct host_buffer_descriptor *input)
{
	struct stat st;

	FILE *fin = fopen(in_file, "r");
	if (fin == NULL) {
		fprintf(stderr, "Invalid input file: %s\n", in_file);
		return 2;
	}

	input->filename = strdup(in_file);
	stat(in_file, &st);
	input->length = st.st_size;

	if (input->length > input->max)
	{
		fprintf(stderr, "Skipping %s: size is too big (%d > %d)\n",
				in_file, input->length, input->max);
		return 2;
	}

	//input->buffer = malloc(input->length * sizeof(*(input->buffer)));
	input->buffer = malloc(MAX_INPUT_LENGTH);
	input->curr = input->buffer;
	size_t n = fread(input->buffer, 1, input->length, fin);
	fclose(fin);

	return (n != input->length);
}

static void usage(const char* exe_name)
{
#ifdef DEBUG
	fprintf(stderr, "**DEBUG BUILD**\n");
#endif //DEBUG
	fprintf(stderr, "Search for a regular expression, like grep\nCan use either the host CPU or UPMEM DPU\n");
	fprintf(stderr, "usage: %s -d -t <search term> <filenames>\n", exe_name);
	fprintf(stderr, "A: context after match\n");
	fprintf(stderr, "B: context before match\n");
	fprintf(stderr, "c: count all matches; suppress normal output\n");
	fprintf(stderr, "d: use DPU\n");
	fprintf(stderr, "t: term to search for\n");
}

/**
 * Main function
 */
int main(int argc, char **argv)
{
	int opt;
	int use_dpu = 1;
	int status;
	uint32_t input_file_count = 0;
	uint32_t allocated_count = 0;
	char *search_term = NULL;
	char **input_files = NULL;
	struct host_context *ctx;
	char pattern[MAX_PATTERN + 1];
	struct grep_options opts;
	struct dpu_set_t dpus, dpu_rank;
	host_results results;

	memset(&results, 0, sizeof(host_results));
	memset(&opts, 0, sizeof(struct grep_options));

	while ((opt = getopt(argc, argv, options)) != -1)
	{
		switch(opt)
		{
		case 'A':
			opts.out_after = strtoul(optarg, NULL, 0);
			break;

		case 'B':
			opts.out_before = strtoul(optarg, NULL, 0);
			break;

		case 'c':
			// count matches
			opts.flags |= (1<<OPTION_FLAG_COUNT_MATCHES);
			break;

		case 'd':
			use_dpu = 1;
			break;

		case 't':
			search_term = optarg;
			strncpy(pattern, search_term, MAX_PATTERN);
			break;

		case 'C':
		case 'D':
		case 'E':
		case 'F':
		case 'G':
		case 'H':
		case 'I':
		case 'T':
		case 'U':
			printf("%s is not supported\n", optarg);
			return -99;
			break;

		default:
			printf("Unrecognized option\n");
			usage(argv[0]);
			return -2;
		}
	}

	// at this point, all the rest of the arguments are files to search through
	int remain_arg_count = argc - optind;
	if (remain_arg_count && strcmp(argv[optind], "-") == 0)
	{
		char buff[TEMP_LENGTH];
		int bytes_remaining;
		allocated_count = 1;
		input_files = malloc(sizeof(char*) * allocated_count);
		bytes_remaining = fread(buff, 1, TEMP_LENGTH, stdin);
		while(bytes_remaining > 0)
		{
			int consumed;
			struct stat st;
			// see if we need more space for file names
			if (input_file_count == allocated_count)
			{
				allocated_count <<= 1;
				input_files = realloc(input_files, sizeof(char*) * allocated_count);
			}
			strtok(buff, "\r\n\t");
			if (stat(buff, &st) == 0 && S_ISREG(st.st_mode))
				input_files[input_file_count++] = strdup(buff);

			consumed = strlen(buff) + 1;
			bytes_remaining -= consumed;
			if (consumed == 1)
				break;

			// scootch the remaining bytes forward and read some more
			memmove(buff, buff + consumed, TEMP_LENGTH - consumed);
			int bytes_read = fread(buff + TEMP_LENGTH - consumed, 1, consumed, stdin);
			if (bytes_read > 0)
				bytes_remaining += bytes_read;
		}
	}
	else
	{
		input_files = malloc(sizeof(char*) * remain_arg_count);
		for (int i=0; i < remain_arg_count; i++)
		{
			struct stat s;
			char *next = argv[i + optind];
			if (stat(next, &s) == 0 && S_ISREG(s.st_mode))
				input_files[input_file_count++] = argv[i + optind];
		}
	}

	// if there are no input files, we have no work to do!
	if (input_file_count == 0)
	{
		printf("No input files!\n");
		usage(argv[0]);
		return -1;
	}

#ifdef BULK_TRANSFER
	printf("Using bulk transfer\n");
#endif // BULK_TRANSFER

	uint8_t rank_id;
	uint64_t rank_status=0; // bitmap indicating if the rank is busy or free
	uint32_t submitted;
	if (use_dpu)
	{
		// allocate all of the DPUS up-front, then check to see how many we got
		status = dpu_alloc(DPU_ALLOCATE_ALL, NULL, &dpus);
		if (status != DPU_OK)
		{
			fprintf(stderr, "Error %i allocating DPUs\n", status);
			return -3;
		}

		dpu_get_nr_ranks(dpus, &rank_count);
		dpu_get_nr_dpus(dpus, &dpu_count);
		dpus_per_rank = dpu_count/rank_count;
		dbg_printf("Got %u dpus across %u ranks (%u dpus per rank)\n", dpu_count, rank_count, dpus_per_rank);

		DPU_ASSERT(dpu_load(dpus, DPU_PROGRAM, NULL));

		if (rank_count > 64)
		{
			printf("Error: too many ranks for a 64-bit bitmask!\n");
			return -4;
		}
		if (input_file_count < dpu_count)
			printf("Warning: fewer input files than DPUs (%u < %u)\n", input_file_count, dpu_count);

		// allocate space for file descriptors
		ctx = calloc(rank_count, sizeof(host_context));
	}

	// prepare the dummy buffer
	sprintf(dummy_buffer, "DUMMY DUMMY DUMMY");

	// Read each input file into main memory
	uint32_t file_index=0;
	uint32_t remaining_file_count = input_file_count;
	dbg_printf("Input file count=%u\n", input_file_count);
	struct host_buffer_descriptor *input;

	// as long as there are still files to process
	while (remaining_file_count)
	{
		uint8_t prepared_file_count;
		uint8_t files_to_prepare = MIN(dpus_per_rank, remaining_file_count);
		input = calloc(dpus_per_rank, sizeof(struct host_buffer_descriptor));

		// prepare enough files to fill the rank
		for (prepared_file_count = 0;
			prepared_file_count < files_to_prepare;
			prepared_file_count++, file_index++)
		{
			// prepare an input buffer descriptor
			input[prepared_file_count].buffer = NULL;
			input[prepared_file_count].length = 0;
			input[prepared_file_count].filename = 0;
			input[prepared_file_count].max = MAX_INPUT_LENGTH;

			// read the file into the descriptor
			switch (read_input_host(input_files[file_index], &input[prepared_file_count]))
			{
			case 1:
				dbg_printf("Error reading file %s\n", input_files[file_index]);
				return 1;
			case 2:
				dbg_printf("Skipping invalid file %s\n", input_files[file_index]);
				continue;
			}

			remaining_file_count--;
		}

		dbg_printf("Prepared %u input descriptors in %p\n", prepared_file_count, input);
		submitted = 0;
		while (!submitted)
		{
			// submit those files to a free rank
			rank_id = 0;
			DPU_RANK_FOREACH(dpus, dpu_rank)
			{
				if (!(rank_status & ((uint64_t)1<<rank_id)))
				{
					rank_status |= ((uint64_t)1<<rank_id);
					dbg_printf("Submitted to rank %u status=0x%lx descriptors %p\n", rank_id, rank_status, input);
					status = search_rank(dpu_rank, rank_id, input, prepared_file_count, pattern, &opts);
					ctx[rank_id].desc = input;
					ctx[rank_id].used = prepared_file_count;
					submitted = 1;
					break;
				}
				rank_id++;
			}

			// as long as we have submitted some files, don't look for finished DPUs
			if (submitted)
				break;

			int ret = check_for_completed_rank(dpus, &rank_status, ctx, &results);
			if (ret == -2)
			{
				printf("A rank has faulted\n");
				status = GREP_FAULT;
				goto done;
			}
		}
	}

	// all files have been submitted; wait for all jobs to finish
	dbg_printf("Waiting for all DPUs to finish\n");
	while (rank_status)
	{
		int ret = check_for_completed_rank(dpus, &rank_status, ctx, &results);
		if (ret == -2)
			goto done;
		usleep(1);
	}

done:
	dpu_free(dpus);

	if (status != GREP_OK)
	{
		fprintf(stderr, "encountered error %u\n", status);
		exit(EXIT_FAILURE);
	}

	printf("Total line count: %u\n", results.total_line_count);
	printf("Total matches: %u\n", results.total_match_count);

	return 0;
}

