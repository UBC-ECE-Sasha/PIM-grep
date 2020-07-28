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

// WRAM input variables - per tasklet
__host uint32_t input_chunk_size[NR_TASKLETS];
__host uint32_t input_start[NR_TASKLETS];

// WRAM output variables
__host uint32_t line_count[NR_TASKLETS];
__host uint32_t match_count[NR_TASKLETS];

// MRAM variables
char __mram_noinit input_buffer[MEGABYTE(31)];
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

	dbg_printf("[%u:%u]: input start: %u length: %u\n", dpu_id, task_id, input_start[task_id], input_chunk_size[task_id]);
	
	// Prepare the input and output descriptors
	// As long as there is at least 1 byte, there is 1 line. But since it does
	// not necessarily end in a newline, we won't detect it.
	line_count[task_id] = 1;
	match_count[task_id] = 0;
	if (input_start[task_id] == -1)
	{
		printf("Thread %u has no data\n", task_id);
		return 0;
	}

	chunk.cache = seqread_alloc();
	chunk.ptr = seqread_init(chunk.cache, input_buffer + input_start[task_id], &chunk.sr);
	chunk.length = input_chunk_size[task_id];

//	perfcounter_config(COUNT_CYCLES, true);

	match_count[task_id] = grep(&chunk, &line_count[task_id]);

	// wait for all tasks to finish
	//printf("[%u] completed in %ld cycles\n", task_id, perfcounter_get());
	//dbg_printf("%u matches in %u lines\n", match_count[task_id], line_count[task_id]);
	return 0;
}

