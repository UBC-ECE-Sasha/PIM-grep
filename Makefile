CC = gcc
CFLAGS = --std=c99 -O3 -g -Wall -Wextra
DPU_OPTS = `dpu-pkg-config --cflags --libs dpu`

# define DEBUG in the source if we are debugging
ifeq ($(DEBUG), 1)
	CFLAGS+=-DDEBUG
endif

# Default NR_TASKLETS
NR_TASKLETS = 16

SOURCE = grep-host.c

.PHONY: default all dpu host clean tags

default: all

all: dpu host

clean:
	$(RM) grep
	$(MAKE) -C dpu-grep $@

dpu:
	DEBUG=$(DEBUG) NR_TASKLETS=$(NR_TASKLETS) $(MAKE) -C dpu-grep

host: $(SOURCE)
	$(CC) $(CFLAGS) -DNR_TASKLETS=$(NR_TASKLETS) $^ -o $@ $(DPU_OPTS)

tags:
	ctags -R -f tags . ~/projects/upmem/upmem-sdk
