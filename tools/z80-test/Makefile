# External single-step z80 test harness.
#
# Builds two binaries:
#   build/z80-test-cz80    native CZ80 (the portable z80 core PicoDrive uses
#                          with _USE_CZ80)
#   build/z80-test-drz80   DrZ80, which is 32-bit ARM assembly, so it is built
#                          with the arm cross compiler and run under qemu-arm
#                          (or copied to a handheld).
#
# Test data is fetched and packed by prepare-tests.py:
#   ./prepare-tests.py            # representative smoke set
#   ./prepare-tests.py --all      # every file in v1

ROOT      := ../..
CORE_DIR  := $(ROOT)/cpu
BUILD     := build

NATIVE_CC      ?= cc
NATIVE_CFLAGS  ?= -O2 -g -std=gnu11 -Wall -Wextra
NATIVE_LDFLAGS ?=
NATIVE_CPPFLAGS := -I. -I$(ROOT)

# v1 has only ed 40 for the R transfers, so the hand-built vectors run too.
TESTS ?= tests/z80/v1 rcheck

CZ80_DIR  := $(BUILD)/cz80
CZ80_OBJS := $(CZ80_DIR)/z80_test.o $(CZ80_DIR)/core_cz80.o $(CZ80_DIR)/cz80.o

.PHONY: all run clean

all: $(BUILD)/z80-test-cz80

$(CZ80_DIR):
	mkdir -p $@

# PICODRIVE_HACKS routes every memory access through PicoDrive's banked z80
# map.  The vectors need plain 64K memory, so build a copy of the core with
# the hack off; the byte callbacks then come from the adapter.
$(CZ80_DIR)/cz80.c: $(CORE_DIR)/cz80/cz80.c | $(CZ80_DIR)
	sed -e 's/\r$$//' $< > $@

$(CZ80_DIR)/cz80.h: $(CORE_DIR)/cz80/cz80.h | $(CZ80_DIR)
	sed -e 's/\r$$//' \
	    -e 's/^#define PICODRIVE_HACKS[[:space:]]*1$$/#define PICODRIVE_HACKS            0/' \
	    $< > $@

$(CZ80_DIR)/cz80.o: $(CZ80_DIR)/cz80.c $(CZ80_DIR)/cz80.h \
		$(CORE_DIR)/cz80/cz80macro.h $(CORE_DIR)/cz80/cz80jmp.c \
		$(CORE_DIR)/cz80/cz80_op.c $(CORE_DIR)/cz80/cz80_opCB.c \
		$(CORE_DIR)/cz80/cz80_opED.c $(CORE_DIR)/cz80/cz80_opXY.c \
		$(CORE_DIR)/cz80/cz80_opXYCB.c | $(CZ80_DIR)
	$(NATIVE_CC) $(NATIVE_CFLAGS) -I$(CZ80_DIR) -I$(CORE_DIR)/cz80 \
		-I$(ROOT) -c $< -o $@

$(CZ80_DIR)/z80_test.o: z80_test.c core.h | $(CZ80_DIR)
	$(NATIVE_CC) $(NATIVE_CFLAGS) $(NATIVE_CPPFLAGS) -c $< -o $@

$(CZ80_DIR)/core_cz80.o: core_cz80.c core.h $(CZ80_DIR)/cz80.h | $(CZ80_DIR)
	$(NATIVE_CC) $(NATIVE_CFLAGS) -I$(CZ80_DIR) -I$(CORE_DIR)/cz80 \
		-I$(ROOT) -c $< -o $@

$(BUILD)/z80-test-cz80: $(CZ80_OBJS)
	$(NATIVE_CC) -o $@ $^ $(NATIVE_LDFLAGS)

run: $(BUILD)/z80-test-cz80
	$(BUILD)/z80-test-cz80 $(TESTS)

# DrZ80 is ARM assembly and uses the arm EABI, so it cannot be linked into the
# native binary; the same runner is built for arm instead.
ARM_CC      ?= arm-linux-gnueabihf-gcc
ARM_CFLAGS  ?= -O2 -g -std=gnu11 -Wall -Wextra -marm -march=armv7-a -mfpu=vfpv3-d16
ARM_RUN     ?= qemu-arm

DRZ80_DIR  := $(BUILD)/drz80
DRZ80_OBJS := $(DRZ80_DIR)/z80_test.o $(DRZ80_DIR)/core_drz80.o $(DRZ80_DIR)/drz80.o

.PHONY: drz80 run-drz80

drz80: $(BUILD)/z80-test-drz80

$(DRZ80_DIR):
	mkdir -p $@

$(DRZ80_DIR)/drz80.S: $(CORE_DIR)/DrZ80/drz80.S | $(DRZ80_DIR)
	sed -e 's/\r$$//' $< > $@

$(DRZ80_DIR)/drz80.o: $(DRZ80_DIR)/drz80.S $(ROOT)/pico/arm_features.h | $(DRZ80_DIR)
	$(ARM_CC) $(ARM_CFLAGS) -I$(ROOT) -c $< -o $@

$(DRZ80_DIR)/z80_test.o: z80_test.c core.h | $(DRZ80_DIR)
	$(ARM_CC) $(ARM_CFLAGS) -DCORE_DRZ80 $(NATIVE_CPPFLAGS) -c $< -o $@

$(DRZ80_DIR)/core_drz80.o: core_drz80.c core.h | $(DRZ80_DIR)
	$(ARM_CC) $(ARM_CFLAGS) -I$(CORE_DIR)/DrZ80 -I$(ROOT) -c $< -o $@

$(BUILD)/z80-test-drz80: $(DRZ80_OBJS)
	$(ARM_CC) -static -o $@ $^

run-drz80: $(BUILD)/z80-test-drz80
	$(ARM_RUN) $(BUILD)/z80-test-drz80 $(TESTS)

clean:
	rm -rf $(BUILD)
