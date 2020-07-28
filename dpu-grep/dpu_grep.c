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
extern struct grep_options options;

static unsigned char READ_BYTE(struct in_buffer_context *_i)
{
	uint8_t ret = *_i->ptr;
	_i->ptr = seqread_get(_i->ptr, sizeof(uint8_t), &_i->sr);
	return ret;
}

uint32_t grep(struct in_buffer_context *buf, uint32_t *line_count)
{
	uint8_t p_index = 0;
	uint32_t match_count = 0;
	uint32_t prev_match_line = -1;
#ifdef DEBUG
	uint8_t task_id = me();
#endif // DEBUG

	for (uint32_t i=0; i < buf->length; i++)
	{
		char c = READ_BYTE(buf);

		// is this a newline?
		if (c == '\n')
			(*line_count)++;

		if (pattern[p_index++] != c)
			p_index = 0;
		else
		{
			// normally, I would not compare an index to a length (index is 0-based,
			// length is 1-based) but in this case it works out perfectly with the
			// post-increment of the index in the 'if' statement above.
			if (p_index == pattern_length)
			{
				dbg_printf("[%u]: found at line +%u\n", task_id, *line_count);
				p_index = 0;

				if (IS_OPTION_SET(&options, OPTION_FLAG_COUNT_MATCHES))
				{
					// only count unique lines that match
					if (*line_count != prev_match_line)
					{
						match_count++;
						prev_match_line = *line_count;
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
		char c = READ_BYTE(buf);
		if (pattern[p_index++] != c)
			break;
		if (p_index == pattern_length)
		{
			dbg_printf("[%u]: found at line +%u\n", task_id, *line_count);
			match_count++;
			break;
		}
	}

done:
	return match_count;
}
