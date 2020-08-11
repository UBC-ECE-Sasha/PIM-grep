#ifndef _GREP_HOST_H_
#define _GREP_HOST_H_

#include "common.h"

enum {
	GREP_OK = 0,
	GREP_INVALID_INPUT,
	GREP_BUFFER_TOO_SMALL,
	GREP_OUTPUT_ERROR,
	GREP_FAULT
};

enum option_flags
{
	OPTION_FLAG_COUNT_MATCHES,
	OPTION_FLAG_OUT_BYTE,
	OPTION_FLAG_MULTIPLE_FILES, // multiple files per DPU
};

/* Try to match the original grep options as closely as possible.
	But they have a lot of backwards-compatibility stuff that is
	ridiculous and unnecessary which makes life difficult for a
	simple proof-of-concept. */
struct grep_options
{
	uint8_t out_before;	/* Lines of leading context. */
	uint8_t out_after;	/* Lines of trailing context. */
	uint32_t flags;		/* most other single-bit options (see OPTION_FLAG_) */
	uint32_t max_files;	/* stop processing after this many files */
}__attribute__((aligned(8)));

typedef struct file_stats
{
	uint32_t line_count; // total lines in the file
	uint32_t match_count; // total matches of the search term
} file_stats;

typedef struct file_descriptor
{
	uint32_t start; 	// offset into host_dpu_descriptor.buffer
	uint32_t length;
} file_descriptor;

typedef struct host_dpu_descriptor
{
	uint32_t file_count; // how many files are processed by this DPU
	uint32_t total_length; // size of the concatenated buffer
	uint32_t perf[NR_TASKLETS];
	char *buffer; // concatenated buffer for this DPU
	char *filename[MAX_FILES_PER_DPU];
	file_descriptor files[MAX_FILES_PER_DPU];
	file_stats stats[MAX_FILES_PER_DPU];
} host_dpu_descriptor;

typedef struct host_rank_context
{
	uint32_t dpu_count; // how many dpus are filled in the descriptor array
	host_dpu_descriptor *dpus; // the descriptors for the dpus in this rank
} host_rank_context;

typedef struct host_results
{
	uint32_t total_line_count;
	uint32_t total_match_count;
	uint32_t total_files;
} host_results;

#endif	/* _GREP_HOST_H_ */

