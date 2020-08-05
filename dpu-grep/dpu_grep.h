/**
 * GREP on DPU.
 */

#ifndef _DPU_GREP_H
#define _DPU_GREP_H

#include "../grep-host.h"
#include <seqread.h> // sequential reader

#ifdef DEBUG
#define dbg_printf(M, ...) printf("%s: " M , __func__, ##__VA_ARGS__)
#else
#define dbg_printf(...)
#endif

#define IS_OPTION_SET(_t, _o) ((_t)->flags & (1<<(_o)))

typedef struct in_buffer_context
{
	char* ptr;
	seqreader_buffer_t cache;
	seqreader_t sr;
	//uint32_t curr;
	uint32_t length;
	uint32_t first_line;
} in_buffer_context;

typedef struct out_buffer_context
{
	__mram_ptr char* buffer; // the entire output buffer in MRAM 
	char* append_ptr; // the append window in WRAM 
	uint32_t append_window; // offset of output buffer mapped by append window (must be multiple of window size) 
	uint32_t curr; // current offset in output buffer in MRAM 
	uint32_t length; // total size of output buffer in bytes 
} out_buffer_context;

uint32_t grep(struct in_buffer_context *buf, uint32_t start, uint32_t file_id);

#endif

