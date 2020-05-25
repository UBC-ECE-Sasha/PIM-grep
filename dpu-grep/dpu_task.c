#include <mram.h>
#include <defs.h>
#include <perfcounter.h>
#include <stdio.h>
#include "alloc.h"
#include "dpu_grep.h"

// WRAM variables
__host uint32_t input_length;
__host uint32_t input_chunk_size;
__host uint32_t pattern_length;
__host uint32_t line_count[NR_TASKLETS];
__host uint32_t match_count[NR_TASKLETS];
__host struct grep_options options;
__host char pattern[64];

// MRAM variables
__host __mram_ptr char* input_buffer;

int main()
{
	struct in_buffer_context chunk;
	uint8_t task_id = me();

	// Prepare the input and output descriptors
	uint32_t input_start = task_id * input_chunk_size;
	if (input_start > input_length)
	{
		//printf("Thread %u has no data\n", idx);
		return 0;
	}

	chunk.cache = seqread_alloc();
	chunk.ptr = seqread_init(chunk.cache, input_buffer + input_start, &chunk.sr);
	chunk.curr = 0;
	chunk.length = MIN(input_length - input_start, input_chunk_size);

	dbg_printf("Thread %u starting at 0x%x length 0x%x\n", task_id, input_start, input_chunk_size);
	perfcounter_config(COUNT_CYCLES, true);

	line_count[task_id] = 0;
	if (task_id == 0)
		line_count[task_id]++;

	match_count[task_id] = grep(&chunk, &line_count[task_id]);
	printf("Tasklet %u: completed in %ld cycles\n", task_id, perfcounter_get());
	//printf("%u matches in %u lines\n", match_count[task_id], line_count[task_id]);
	return 0;
}

