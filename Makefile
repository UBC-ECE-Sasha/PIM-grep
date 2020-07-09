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

ifeq ($(BULK), 1)
	CFLAGS+=-DBULK_TRANSFER
endif

SOURCE = grep-host.c

.PHONY: default all dpu host clean tags

default: all

all: dpu host

clean:
	$(RM) grep
	$(MAKE) -C dpu-grep $@

dpu:
	DEBUG=$(DEBUG_DPU) NR_DPUS=$(NR_DPUS) NR_TASKLETS=$(NR_TASKLETS) $(MAKE) -C dpu-grep

host: $(SOURCE)
	$(CC) $(CFLAGS) -DNR_DPUS=$(NR_DPUS) -DNR_TASKLETS=$(NR_TASKLETS) $^ -o $@ $(DPU_OPTS)

tags:
	ctags -R -f tags . ~/projects/upmem/upmem-sdk
