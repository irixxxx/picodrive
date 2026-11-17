# External single-step SH-2 test harness.
#
# Builds two backends that run the same SingleStepTests/sh4 vectors:
#   build/sh2-test      MAME SH-2 interpreter (cpu/sh2/mame)
#   build/sh2-test-drc  SH-2 recompiler (cpu/sh2/compiler.c), built with
#                       SH2_DRC_SINGLE_STEP so one step retires one insn
#
# Test data is fetched by prepare-tests.py:
#   ./prepare-tests.py            # every file in the repo (about 92 MB)
#   ./prepare-tests.py --list     # list remote file names

ROOT     := ../..
CORE_DIR := $(ROOT)/cpu/sh2
DRC_DIR  := $(ROOT)/cpu/drc
BUILD    := build

NATIVE_CC     ?= cc

# gcc for 32 bit arm may default to thumb.  The recompiler emits arm code and
# calls its helpers by their address, so it needs arm mode (like m68k-test).
ifneq ($(shell $(NATIVE_CC) -dumpmachine 2>/dev/null | grep -c '^arm-'),0)
ARM_MARM := -marm
endif

NATIVE_CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra $(ARM_MARM)

CPPFLAGS := -I. -I$(ROOT) -I$(CORE_DIR) -I$(CORE_DIR)/mame

DRC_CPPFLAGS := $(CPPFLAGS) -I$(DRC_DIR)
DRC_CFLAGS   := -DDRC_SH2 -DSH2_DRC_SINGLE_STEP

TESTS ?= tests/sh4/v1

OBJS := $(BUILD)/sh2_test.o $(BUILD)/core_sh2.o $(BUILD)/core_bus.o \
	$(BUILD)/sh2pico.o $(BUILD)/sh2dasm.o

OBJS_DRC := $(BUILD)/sh2_test.o $(BUILD)/core_drc.o $(BUILD)/core_bus.o \
	$(BUILD)/sh2_drc.o $(BUILD)/cmn.o $(BUILD)/sh2dasm.o

.PHONY: all run run-drc clean

all: $(BUILD)/sh2-test $(BUILD)/sh2-test-drc

$(BUILD):
	mkdir -p $@

$(BUILD)/sh2_test.o: sh2_test.c core.h $(CORE_DIR)/mame/sh2dasm.h | $(BUILD)
	$(NATIVE_CC) $(NATIVE_CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/core_bus.o: core_bus.c core.h | $(BUILD)
	$(NATIVE_CC) $(NATIVE_CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/core_sh2.o: core_sh2.c core.h $(CORE_DIR)/sh2.h | $(BUILD)
	$(NATIVE_CC) $(NATIVE_CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/sh2pico.o: $(CORE_DIR)/mame/sh2pico.c $(CORE_DIR)/mame/sh2.c \
		$(CORE_DIR)/sh2.h | $(BUILD)
	$(NATIVE_CC) $(NATIVE_CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/sh2dasm.o: $(CORE_DIR)/mame/sh2dasm.c $(CORE_DIR)/mame/sh2dasm.h | $(BUILD)
	$(NATIVE_CC) $(NATIVE_CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/core_drc.o: core_drc.c core.h $(CORE_DIR)/sh2.h | $(BUILD)
	$(NATIVE_CC) $(NATIVE_CFLAGS) $(DRC_CPPFLAGS) $(DRC_CFLAGS) -c $< -o $@

$(BUILD)/sh2_drc.o: $(CORE_DIR)/compiler.c $(CORE_DIR)/compiler.h \
		$(CORE_DIR)/sh2.h $(DRC_DIR)/cmn.h | $(BUILD)
	$(NATIVE_CC) $(NATIVE_CFLAGS) $(DRC_CPPFLAGS) $(DRC_CFLAGS) -c $< -o $@

$(BUILD)/cmn.o: $(DRC_DIR)/cmn.c $(DRC_DIR)/cmn.h | $(BUILD)
	$(NATIVE_CC) $(NATIVE_CFLAGS) $(DRC_CPPFLAGS) $(DRC_CFLAGS) -c $< -o $@

$(BUILD)/sh2-test: $(OBJS)
	$(NATIVE_CC) -o $@ $^

$(BUILD)/sh2-test-drc: $(OBJS_DRC)
	$(NATIVE_CC) -o $@ $^

run: $(BUILD)/sh2-test
	./$(BUILD)/sh2-test $(TESTS)

run-drc: $(BUILD)/sh2-test-drc
	./$(BUILD)/sh2-test-drc $(TESTS)

clean:
	rm -rf $(BUILD)
