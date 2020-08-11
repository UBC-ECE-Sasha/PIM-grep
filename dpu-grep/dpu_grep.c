#include <assert.h>
#include <string.h>  // memcpy
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <mram.h>
#include <defs.h>
#include "dpu_grep.h"

extern char pattern[64];
extern uint32_t pattern_length;
extern uint32_t file_count;
extern struct grep_options options;
//extern uint32_t file_size[MAX_FILES_PER_DPU];
//extern uint32_t file_start[MAX_FILES_PER_DPU];
extern file_descriptor file[MAX_FILES_PER_DPU];
extern file_stats stats[MAX_FILES_PER_DPU];

static unsigned char READ_BYTE(struct in_buffer_context *_i)
{
	uint8_t ret = *_i->ptr;
	_i->ptr = seqread_get(_i->ptr, sizeof(uint8_t), &_i->sr);
	return ret;
}

uint32_t grep(struct in_buffer_context *buf, uint32_t start, uint32_t file_id)
{
	uint32_t i;
	uint8_t p_index = 0;
	uint32_t prev_match_line = -1;
#ifdef DEBUG
	uint8_t task_id = me();
#endif // DEBUG

	for (i=0; i < buf->length; i++)
	{
		char c = READ_BYTE(buf);

		// if we are past the end of the file, go to the next one
		if (start + i > file[file_id].length)
		{
			file_id++;
			if (file_id == file_count)
				return 0;
			p_index = 0;
			prev_match_line = -1;
		}

		// is this a newline?
		if (c == '\n')
			stats[file_id].line_count++;

		if (pattern[p_index++] != c)
			p_index = 0;
		else
		{
			// normally, I would not compare an index to a length (index is 0-based,
			// length is 1-based) but in this case it works out perfectly with the
			// post-increment of the index in the 'if' statement above.
			if (p_index == pattern_length)
			{
				dbg_printf("[%u]: match in file %u\n", task_id, file_id);
				p_index = 0;

				if (IS_OPTION_SET(&options, OPTION_FLAG_COUNT_MATCHES))
				{
					// only count unique lines that match
					if (stats[file_id].line_count != prev_match_line)
					{
						stats[file_id].match_count++;
						prev_match_line = stats[file_id].line_count;
					}
				}
				else
				{
					// if we only need to find the first match, we are done
					goto done;
				}
			}
		}
	}

	// if we hit the end of our allotted buffer, but are in the middle of a
	// comparison (a possible hit) and there is still data left in the file,
	// keep going a bit more
#ifdef DEBUG
	if (p_index)
		printf("[%u]: going into overtime!\n", task_id);
#endif // DEBUG

	while (p_index)
	{
		i++; // continue to use the byte counter from the previous for loop
		char c = READ_BYTE(buf);

		// if we are past the end of the file,  we're done
		if (start + i > file[file_id].length)
			break;

		if (pattern[p_index++] != c)
			break;
		if (p_index == pattern_length)
		{
			dbg_printf("[%u]: match in file %u\n", task_id, file_id);
			stats[file_id].match_count++;
			break;
		}
	}

done:
	return 0;
}
