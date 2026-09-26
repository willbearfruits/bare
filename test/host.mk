# Host test harness: the portable core built as ordinary 32-bit Linux programs (see test/host.h).
# Included by the main Makefile (`make test`, `make bench`, `make shots`); also usable on its own to measure another
# checkout: make -f test/host.mk HB_ROOT=/path/to/other/tree HOST_OUT=/tmp/x HOST_EXTRA=-DHB_BASELINE bench
HB_ROOT    ?= .
TEST_DIR   ?= $(dir $(lastword $(MAKEFILE_LIST)))
HOST_OUT   ?= build/host
HOST_CC    := clang -m32 -march=i686
# the core gets the kernel's code-generation constraints (integer only, no SSE) so cycle counts carry over
HOST_CORE_FLAGS := -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
                   -ffreestanding -fno-builtin -mno-80387 -mno-mmx -mno-sse -I$(HB_ROOT)/core $(HOST_EXTRA)
HOST_TEST_FLAGS := -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -mno-sse -I$(HB_ROOT)/core -I$(TEST_DIR) $(HOST_EXTRA)
HOST_CORE_SRC := $(wildcard $(HB_ROOT)/core/*.c)
HOST_CORE_OBJ := $(patsubst $(HB_ROOT)/core/%.c,$(HOST_OUT)/core/%.o,$(HOST_CORE_SRC))
HOST_PROGS ?= shots bench render stretchq check video         # another checkout: HOST_PROGS="shots bench render"
# Doom's engine and glue, as in the kernel: its variables in sections of their own (core/doomhost.c puts them back)
HOST_DOOM_SRC := $(wildcard $(HB_ROOT)/third_party/doom/*.c $(HB_ROOT)/third_party/doom/bare/*.c)
HOST_DOOM_OBJ := $(patsubst $(HB_ROOT)/%.c,$(HOST_OUT)/%.o,$(HOST_DOOM_SRC))
HOST_DOOM_FLAGS := -std=gnu99 -O2 -ffreestanding -fno-builtin -mno-80387 -mno-mmx -mno-sse -w -nostdlibinc \
                   -I$(HB_ROOT)/third_party/doom/bare/libc -I$(HB_ROOT)/third_party/doom -I$(HB_ROOT)/third_party/doom/bare \
                   -I$(HB_ROOT)/core -DBARE_DOOM $(HOST_EXTRA)

$(HOST_OUT)/core/libc.o: $(HB_ROOT)/core/libc.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CORE_FLAGS) -Dmemcpy=hb_memcpy -Dmemmove=hb_memmove -Dmemset=hb_memset -Dmemcmp=hb_memcmp -Dstrlen=hb_strlen -c $< -o $@
$(HOST_OUT)/core/%.o: $(HB_ROOT)/core/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CORE_FLAGS) -MMD -MP -c $< -o $@
$(HOST_OUT)/test/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_TEST_FLAGS) -MMD -MP -c $< -o $@
$(HOST_OUT)/third_party/%.o: $(HB_ROOT)/third_party/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_DOOM_FLAGS) -MMD -MP -c $< -o $@
	llvm-objcopy --rename-section .data=doom_data --rename-section .bss=doom_bss $@
$(addprefix $(HOST_OUT)/,$(HOST_PROGS)): $(HOST_OUT)/%: $(HOST_OUT)/test/%.o $(HOST_OUT)/test/host.o $(HOST_CORE_OBJ) $(HOST_DOOM_OBJ)
	$(HOST_CC) -o $@ $^ -lm

host-progs: $(addprefix $(HOST_OUT)/,$(HOST_PROGS))

-include $(HOST_CORE_OBJ:.o=.d) $(HOST_DOOM_OBJ:.o=.d) $(wildcard $(HOST_OUT)/test/*.d)
.PHONY: host-progs
