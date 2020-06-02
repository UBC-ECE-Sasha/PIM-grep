#define _DEFAULT_SOURCE // needed for S_ISREG() and strdup
#include <dpu.h>
#include <dpu_memory.h>
#include <dpu_log.h>
#include <dpu_management.h>
#include <unistd.h>

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>

#include "grep-host.h"

#define DPU_PROGRAM "dpu-grep/grep.dpu"
#define MAX_INPUT_LENGTH MEGABYTE(32)
#define MAX_OUTPUT_LENGTH MEGABYTE(32)
#define MIN_CHUNK_SIZE 128 // not worthwhile making another tasklet work for data less than this
#define MAX_PATTERN 63
#define TEMP_LENGTH 256

struct {
	struct dpu_set_t dpu;
	struct dpu_set_t dpus;
	struct host_buffer_context *input;
} working_dpus[NR_DPUS];

int used_dpus;

const char options[]="dt:";

// === from original 'grep' ===
// === from original 'grep' ===

/**
 * Prepare the DPU context by copying the input buffer to MRAM and
 * uploading the program to the DPU.
 */
int search_dpu(struct dpu_set_t *dpu, struct host_buffer_context *input, const char* pattern, struct grep_options* opts)
{
	printf("searching for pattern: %s\n", pattern);
	// Set up and run the program on the DPU
	uint32_t input_buffer_start = MEGABYTE(1);
	//uint32_t output_buffer_start = ALIGN(input_buffer_start + input->length, 64);
	uint32_t pattern_length = strlen(pattern);

	// Must be a multiple of 8 to ensure the last write to MRAM is also a multiple of 8
	//uint32_t output_length = ALIGN(MEGABYTE(64) - output_buffer_start, 8);
	//printf("input: 0x%x length 0x%x output: 0x%x max length 0x%x\n",
	//	input_buffer_start, input->length, output_buffer_start, output_length);

	uint32_t chunk_size = MAX(MIN_CHUNK_SIZE, ALIGN(input->length / NR_TASKLETS, 16));
	//printf("chunk size: 0x%x\n", chunk_size);


	DPU_ASSERT(dpu_copy_to(*dpu, "input_length", 0, &input->length, sizeof(uint32_t)));
	DPU_ASSERT(dpu_copy_to(*dpu, "input_buffer", 0, &input_buffer_start, sizeof(uint32_t)));
	DPU_ASSERT(dpu_copy_to(*dpu, "input_chunk_size", 0, &chunk_size, sizeof(uint32_t)));
	//DPU_ASSERT(dpu_copy_to(*dpu, "output_length", 0, &output_length, sizeof(uint32_t)));
	//DPU_ASSERT(dpu_copy_to(*dpu, "output_buffer", 0, &output_buffer_start, sizeof(uint32_t)));
	DPU_ASSERT(dpu_copy_to(*dpu, "options", 0, opts, ALIGN(sizeof(struct grep_options), 8)));
	DPU_ASSERT(dpu_copy_to(*dpu, "pattern_length", 0, &pattern_length, sizeof(uint32_t)));
	DPU_ASSERT(dpu_copy_to(*dpu, "pattern", 0, pattern, ALIGN(pattern_length, 8)));

	// dpu_copy_to_mram allows us to pass a variable size buffer to a variable
	// location. That means it is more flexible, and we don't have to know the
	// size of the input buffer ahead of time in the DPU program.
	dpu_copy_to_mram(dpu->dpu, input_buffer_start, (unsigned char*)input->buffer, ALIGN(input->length, 8), 0);
	
	if (dpu_launch(*dpu, DPU_ASYNCHRONOUS) != 0)
	{
		printf("DPU launch failed\n");
		return GREP_INVALID_INPUT;
	}

	printf("Search started\n");
	return GREP_OK;
}

int read_results_dpu_rank(struct dpu_set_t dpu_rank, struct host_buffer_context *output)
{
	struct dpu_set_t dpu;
	uint32_t line_count[NR_TASKLETS];
	uint32_t match_count[NR_TASKLETS];

	DPU_ASSERT(dpu_rank.kind == DPU_SET_RANKS);
	DPU_FOREACH(dpu_rank, dpu)
	{

		DPU_ASSERT(dpu_log_read(dpu, stdout));


		// Get the results back from each individual DPU in the rank
		//dpu_copy_from_mram(dpu.dpu, (unsigned char*)output->buffer, output_buffer_start, output->length, 0);
		DPU_ASSERT(dpu_copy_from(dpu, "line_count", 0, line_count, sizeof(uint32_t) * NR_TASKLETS));
		DPU_ASSERT(dpu_copy_from(dpu, "match_count", 0, match_count, sizeof(uint32_t) * NR_TASKLETS));

		// aggregate the statistics
		for (uint8_t i=0; i < NR_TASKLETS; i++)
		{
			output->line_count += line_count[i];
			output->match_count += match_count[i];
		}
	}

	return 0;
}

int read_status_dpu_rank(struct dpu_set_t dpu_rank)
{
	struct dpu_t *dpu;
	DPU_ASSERT(dpu_rank.kind == DPU_SET_RANKS);
	struct dpu_rank_t *rank = dpu_rank_from_set(dpu_rank);
	STRUCT_DPU_FOREACH(rank, dpu)
	{
		printf("DPU slice id: %u member id: %u\n", dpu_get_slice_id(dpu), dpu_get_member_id(dpu));
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
static int read_input_host(char *in_file, struct host_buffer_context *input)
{
	struct stat st;

	FILE *fin = fopen(in_file, "r");
	if (fin == NULL) {
		fprintf(stderr, "Invalid input file: %s\n", in_file);
		return 1;
	}

	stat(in_file, &st);
	input->length = st.st_size;

	if (input->length > input->max)
	{
		fprintf(stderr, "input_size is too big (%d > %d)\n",
				input->length, input->max);
		return 1;
	}

	input->buffer = malloc(input->length * sizeof(*(input->buffer)));
	input->curr = input->buffer;
	size_t n = fread(input->buffer, sizeof(*(input->buffer)), input->length, fin);
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
	struct host_buffer_context *input;
	struct host_buffer_context output;
	char pattern[MAX_PATTERN + 1];
	struct grep_options opts;
	struct dpu_set_t dpus, dpu;

	memset(&opts, 0, sizeof(struct grep_options));

	output.buffer = NULL;
	output.length = 0;
	output.max = MAX_OUTPUT_LENGTH;

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
			usage(argv[0]);
			return -2;
		}
	}

	// at this point, all the rest of the arguments are files to search through
	int remain_arg_count = argc - optind;

	if (remain_arg_count && strcmp(argv[optind], "-") == 0)
	{
		int consumed = TEMP_LENGTH;
		char buff[TEMP_LENGTH];
		allocated_count = 1;
		input_files = malloc(sizeof(char*) * allocated_count);
		while(fread(buff + TEMP_LENGTH - consumed, consumed, 1, stdin) > 0)
		{
			struct stat st;
			// see if we need more space for file names
			if (input_file_count == allocated_count)
			{
				allocated_count <<= 1;
				input_files = realloc(input_files, sizeof(char*) * allocated_count);
			}
			strtok(buff, "\r\n\t");
			consumed = strlen(buff) + 1;
			dbg_printf("filename: %s (%u)\n", buff, consumed);
			if (stat(buff, &st) == 0 && S_ISREG(st.st_mode))
				input_files[input_file_count++] = strdup(buff);
			// scootch the remaining bytes forward in tmp
			memmove(buff, buff + consumed, TEMP_LENGTH - consumed);
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
		usage(argv[0]);
		return -1;
	}

	uint32_t rank_count, dpu_count, dpus_per_rank;
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

		DPU_FOREACH(dpus, dpu)
			DPU_ASSERT(dpu_load(dpu, DPU_PROGRAM, NULL));

		if (input_file_count < dpu_count)
			printf("Warning: fewer input files than DPUs (%u < %u)\n", input_file_count, dpu_count);
	}

	// Read each input file into main memory
	uint32_t file_index=0;
	uint32_t remaining_file_count = input_file_count;
	dbg_printf("Input file count=%u\n", input_file_count);
	while (remaining_file_count)
	{
		uint8_t prepared_file_count;
		for (prepared_file_count = 0;
			prepared_file_count < MIN(dpus_per_rank, remaining_file_count);
			prepared_file_count++)
		{
			//dbg_printf("remaining file_count: %u\n", remaining_file_count);
			// prepare an input buffer descriptor
			input = malloc(sizeof(struct host_buffer_context) * dpus_per_rank);
			input[prepared_file_count].buffer = NULL;
			input[prepared_file_count].length = 0;
			input[prepared_file_count].max = MAX_INPUT_LENGTH;

			// read the file into the descriptor
			if (read_input_host(input_files[file_index++], &input[prepared_file_count]))
				return 1;

			remaining_file_count--;
		}
		dbg_printf("Prepared %u input descriptors\n", prepared_file_count);

		// schedule full ranks, if we have enough remaining files
		DPU_RANK_FOREACH(dpus, dpu)
		{
			uint32_t nr_dpus;
			bool done, fault;

			dpu_get_nr_dpus(dpu, &nr_dpus);
			printf("There are %u dpus in this rank\n", nr_dpus);
			DPU_ASSERT(dpu_status(dpu, &done, &fault));
			if (fault)
			{
				printf("a DPU in set XXX has a fault\n");
			}
			if (done)
			{
				printf("all DPUs in set XXX are done\n");
				read_results_dpu_rank(dpu, &output);
			}
			//status = search_dpu(&working_dpus[dpu_index].dpu, input, pattern, &opts);

			// check to see if anything has completed
			//completed_dpus(&output);
			read_status_dpu_rank(dpu);
		}

		//completed_dpus(&output);
	}

	// all files have been submitted; wait for all jobs to finish
	printf("Waiting for all DPUs to finish\n");

	dpu_free(dpus);

	if (status != GREP_OK)
	{
		fprintf(stderr, "encountered error %u\n", status);
		exit(EXIT_FAILURE);
	}

	printf("Total line count: %u\n", output.line_count);
	printf("Total matches: %u\n", output.match_count);

	return 0;
}

