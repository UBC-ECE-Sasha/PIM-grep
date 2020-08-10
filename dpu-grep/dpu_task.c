#include <mram.h>
#include <defs.h>
#include <perfcounter.h>
#include <stdio.h>
#include "alloc.h"
#include "dpu_grep.h"
#include <string.h>

// WRAM global variables
__host struct grep_options options;
__host uint32_t pattern_length;
__host char pattern[64];

// WRAM input variables - per DPU
__host uint32_t dpu_id;
__host uint32_t total_length;
__host uint32_t file_count;
__host uint32_t chunk_size; // work for each tasklet to do
__host uint32_t file_size[MAX_FILES_PER_DPU];
__host uint32_t file_start[MAX_FILES_PER_DPU];

// WRAM output variables
__host file_stats stats[MAX_FILES_PER_DPU];
__host uint32_t perf[NR_TASKLETS];

// MRAM variables
char __mram_noinit input_buffer[MEGABYTE(62)];
char __mram_noinit output_buffer[MEGABYTE(1)];

int main()
{
	struct in_buffer_context chunk;
	uint8_t task_id = me();

	if (task_id == 0)
	{
		// clear the heap
		mem_reset();

		dbg_printf("options: [%c]\n",
			(IS_OPTION_SET(&options, OPTION_FLAG_COUNT_MATCHES)) ? 'C' : ' '
		);

		dbg_printf("Sequential reader buffer size: %u\n", SEQREAD_CACHE_SIZE);
		//dbg_printf("[%u.%u]: pattern length: %u\n", dpu_id, task_id, pattern_length);
		//dbg_printf("[%u.%u]: pattern: %s\n", dpu_id, task_id, pattern);
	}

	uint32_t input_start = chunk_size * task_id;
	uint32_t input_length = chunk_size;

	if (input_start > total_length)
	{
		printf("Thread %u has no data\n", task_id);
		return 0;
	}

	// make sure we don't go past the end of the last chunk
	if (input_start + input_length > total_length)
		input_length = total_length - input_start;

	dbg_printf("[%u:%u]: input start: %u length: %u\n", dpu_id, task_id, input_start, input_length);

	chunk.cache = seqread_alloc();
	chunk.ptr = seqread_init(chunk.cache, input_buffer + input_start, &chunk.sr);
	chunk.length = input_length;

	// which file are we starting with?
	uint32_t file_id=0;
	while (input_start < file_start[file_id])
		file_id++;

	// As long as there is at least 1 byte, there is 1 line. But since it does
	// not necessarily end in a newline, we won't detect it.
	stats[file_id].line_count = 1;
	stats[file_id].match_count = 0;

	perfcounter_config(COUNT_INSTRUCTIONS, true);
	grep(&chunk, input_start, file_id);
	perf[task_id] = perfcounter_get();

	return 0;
}

