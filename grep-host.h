#ifndef _GREP_HOST_H_
#define _GREP_HOST_H_

#include "common.h"

enum {
	GREP_OK = 0,
	GREP_INVALID_INPUT,
	GREP_BUFFER_TOO_SMALL,
	GREP_OUTPUT_ERROR
};

enum
{
	OPTION_FLAG_COUNT_MATCHES,
	OPTION_FLAG_OUT_BYTE,
};

/* Try to match the original grep options as closely as possible.
	But they have a lot of backwards-compatibility stuff that is
	ridiculous and unnecessary which makes life difficult for a
	simple proof-of-concept. */
struct grep_options
{
	uint8_t out_before;	/* Lines of leading context. */
	uint8_t out_after;	/* Lines of trailing context. */
	uint32_t flags;		/* most other single-bit options */
}__attribute__((aligned(8)));

typedef struct host_buffer_context
{
	char *filename;
	char *buffer;
	char *curr;
	uint32_t length;
	uint32_t max;
	uint32_t line_count; // total lines in the file
	uint32_t match_count; // total matches of the search term
} host_buffer_context;

#endif	/* _GREP_HOST_H_ */

