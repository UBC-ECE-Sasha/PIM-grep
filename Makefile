CC = gcc
CFLAGS = --std=c99 -O3 -g -Wall -Wextra -I PIM-common/common/include
DPU_OPTS = `dpu-pkg-config --cflags --libs dpu`

# define DEBUG in the source if we are debugging
ifeq ($(DEBUG_CPU), 1)
	CFLAGS+=-DDEBUG
endif

ifeq ($(DEBUG_DPU), 1)
	CFLAGS+=-DDEBUG_DPU
endif

# Default NR_TASKLETS
NR_TASKLETS = 16

# Default number of DPUs
NR_DPUS = 1

# Bulk (dpu_prepare_xfer) is default
BULK = 1

# Statistics are on by default
STATS = 1

SEQREAD_CACHE_SIZE=128

ifeq ($(BULK), 1)
	CFLAGS+=-DBULK_TRANSFER
endif

ifeq ($(STATS), 1)
	CFLAGS+=-DSTATISTICS
endif

SOURCE = grep-host.c

.PHONY: default all dpu host clean tags

default: all

all: dpu host

clean:
	$(RM) host-*
	$(MAKE) -C dpu-grep $@

dpu:
	DEBUG=$(DEBUG_DPU) NR_DPUS=$(NR_DPUS) NR_TASKLETS=$(NR_TASKLETS) SEQREAD_CACHE_SIZE=$(SEQREAD_CACHE_SIZE) $(MAKE) -C dpu-grep

host: $(SOURCE)
	$(CC) $(CFLAGS) -DNR_TASKLETS=$(NR_TASKLETS) $^ -o $@-$(NR_TASKLETS) $(DPU_OPTS)

tags:
	ctags -R -f tags . ~/projects/upmem/upmem-sdk
