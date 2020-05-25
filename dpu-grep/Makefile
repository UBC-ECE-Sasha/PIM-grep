CC           = dpu-upmem-dpurte-clang
CFLAGS       = -O2 -flto -g -Wall

STACK_SIZE_DEFAULT = 256
CFLAGS += -DNR_TASKLETS=$(NR_TASKLETS)
CFLAGS += -DSTACK_SIZE_DEFAULT=$(STACK_SIZE_DEFAULT)

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
	$(CC) $(CFLAGS)  $^ -o $@

