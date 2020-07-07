#include <mram.h>
#include <defs.h>
#include <perfcounter.h>
#include <stdio.h>
#include "alloc.h"
#include "dpu_grep.h"
#include <string.h>

// WRAM variables
__host uint32_t line_count[NR_TASKLETS];
__host uint32_t match_count[NR_TASKLETS];
__host struct grep_options options;
__host uint32_t dpu_id;
__host uint32_t input_length;
__host uint32_t input_chunk_size;
__host uint32_t pattern_length;
__host char pattern[64];


// MRAM variables
//char __mram_noinit input_buffer[MEGABYTE(4)] __attribute__ ((aligned (4)));
char __mram input_buffer[1048576];

int main()
{
	struct in_buffer_context chunk;
	uint8_t task_id = me();

	if (task_id == 0)
	{
		dbg_printf("options:\n");
		if (IS_OPTION_SET(&options, OPTION_FLAG_COUNT_MATCHES))
			dbg_printf("   count matches\n");

	//dbg_printf("Sequential reader buffer size: %u\n", SEQREAD_CACHE_SIZE);
	dbg_printf("[%u.%u]: input length: %u\n", dpu_id, task_id, input_length);
	dbg_printf("[%u:%u]: input chunk size: %u\n", dpu_id, task_id, input_chunk_size);
	dbg_printf("[%u.%u]: pattern length: %u\n", dpu_id, task_id, pattern_length);
	dbg_printf("[%u.%u]: pattern: %s\n", dpu_id, task_id, pattern);
	}
	
	// Prepare the input and output descriptors
	uint32_t input_start = task_id * input_chunk_size;
	if (input_start > input_length)
	{
		printf("Thread %u has no data\n", task_id);
		return 0;
	}

	chunk.cache = seqread_alloc();
	chunk.ptr = seqread_init(chunk.cache, input_buffer + input_start, &chunk.sr);
	//chunk.curr = 0;
	chunk.length = MIN(input_length - input_start, input_chunk_size);

//	perfcounter_config(COUNT_CYCLES, true);

	line_count[task_id] = 0;
	if (task_id == 0)
		line_count[task_id]++;

	match_count[task_id] = grep(&chunk, &line_count[task_id]);
	//printf("[%u] completed in %ld cycles\n", task_id, perfcounter_get());
	printf("%u matches in %u lines\n", match_count[task_id], line_count[task_id]);
	return 0;
}

