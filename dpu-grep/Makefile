CC           = dpu-upmem-dpurte-clang
CFLAGS       = -O2 -flto -g -Wall

NR_TASKETS=1
STACK_SIZE_DEFAULT = 128
SEQREAD_CACHE_SIZE=256
CFLAGS += -DNR_TASKLETS=$(NR_TASKLETS)
CFLAGS += -DSTACK_SIZE_DEFAULT=$(STACK_SIZE_DEFAULT)
CFLAGS += -DSEQREAD_CACHE_SIZE=$(SEQREAD_CACHE_SIZE)
CFLAGS += -I../PIM-common/common/include

# define DEBUG in the source if we are debugging
ifeq ($(DEBUG), 1)
	CFLAGS+=-DDEBUG
endif

SOURCES = dpu_task.c dpu_grep.c
TARGET_DPU = grep.dpu

.PHONY: default all clean

default: all

all: $(TARGET_DPU)

clean:
	$(RM) $(TARGET_DPU)

$(TARGET_DPU): $(SOURCES)
	$(CC) $(CFLAGS)  $^ -o $@-$(NR_TASKLETS)

