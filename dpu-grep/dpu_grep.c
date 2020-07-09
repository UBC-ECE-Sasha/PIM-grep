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

/* Flags controlling the style of output. */
/*static enum
{
  BINARY_BINARY_FILES,
  TEXT_BINARY_FILES,
  WITHOUT_MATCH_BINARY_FILES
} binary_files;		 How to handle binary files.  */

/* Options for output as a list of matching/non-matching files */
/*static enum
{
  LISTFILES_NONE,
  LISTFILES_MATCHING,
  LISTFILES_NONMATCHING,
} list_files; */

//static int filename_mask;	/* If zero, output nulls after filenames.  */
//static bool out_quiet;		/* Suppress all normal output. */
//static bool out_invert;		/* Print nonmatching stuff. */
//static bool out_line;		/* Print line numbers. */
//static bool out_byte;		/* Print byte offsets. */
//static intmax_t max_count;	/* Max number of selected
//                                   lines from an input file.  */
//static bool line_buffered;	/* Use line buffering.  */

/* Internal variables to keep track of byte count, context, etc. */
//static uintmax_t totalcc;	/* Total character count before bufbeg. */
//static char const *lastnl;	/* Pointer after last newline counted. */
//static char *lastout;		/* Pointer after last character output;
//                                   NULL if no character has been output
//                                   or if it's conceptually before bufbeg. */
//static intmax_t outleft;	/* Maximum number of selected lines.  */
//static intmax_t pending;	/* Pending lines of output.
//                                   Always kept 0 if out_quiet is true.  */
//static bool dev_null_output;	/* Stdout is known to be /dev/null.  */
//static bool binary;		/* Use binary rather than text I/O.  */

//static char *buffer;		/* Base of buffer. */
//static size_t bufalloc;		/* Allocated buffer size, counting slop. */
//static int bufdesc;		/* File descriptor. */
//static char *bufbeg;		/* Beginning of user-visible stuff. */
//static char *buflim;		/* Limit of user-visible stuff. */
//static size_t pagesize;		/* alignment of memory pages */
//static off_t bufoffset;		/* Read offset.  */
/*static off_t after_last_match;	 Pointer after last matching line that
                                   would have been output if we were
                                   outputting characters. */
/*static bool skip_empty_lines;	 Skip empty lines in data.  */

static unsigned char READ_BYTE(struct in_buffer_context *_i)
{
	uint8_t ret = *_i->ptr;
	_i->ptr = seqread_get(_i->ptr, sizeof(uint8_t), &_i->sr);
	//_i->curr++;
	return ret;
}

/* Search a given (non-directory) file.  Return a count of lines printed.
   Set *INEOF to true if end-of-file reached.  */
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
