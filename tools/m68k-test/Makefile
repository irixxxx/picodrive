# External single-step 68000 test harness.
#
# Builds four binaries:
#   build/m68k-test-fame      native (aarch64) FAME
#   build/m68k-test-musashi   native (aarch64) Musashi
#   build/m68k-test-cyclone   32-bit ARMv7 Cyclone (run under qemu-arm)
#   build/m68k-test-arm9      32-bit ARMv4T Cyclone (run under qemu-arm -cpu arm946)
#
# The Cyclone binaries must be 32-bit because struct Cyclone.membase is an int.
# The ARMv7 one is built with the arm-linux-gnueabihf cross toolchain and a
# hard-float sysroot (see README.md); the ARM9 one is static and only needs
# the soft-float arm-linux-gnueabi toolchain.

ROOT      := ../..
CORE_DIR  := $(ROOT)/cpu
BUILD     := build

NATIVE_CC      ?= cc
NATIVE_CFLAGS  ?= -O2 -g -std=gnu11 -Wall -Wextra
NATIVE_LDFLAGS ?=
NATIVE_CPPFLAGS := -I. -I$(ROOT)

ARM_CC         ?= arm-linux-gnueabihf-gcc
ARM_SYSROOT    ?= $(HOME)/opt/linux-cross/arm-linux-gnueabihf
# Cyclone.s is ARM code.  Force the C side to ARM too: this toolchain
# defaults to Thumb, and a Thumb BL into the ARM Cyclone entry points is
# decoded as garbage (illegal instruction under qemu).
ARM_ARCH_FLAGS := -marm -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard
ARM_CFLAGS     ?= -O2 -g -std=gnu11 -Wall -Wextra $(ARM_ARCH_FLAGS) --sysroot=$(ARM_SYSROOT)
ARM_CPPFLAGS   := -I. -I$(CORE_DIR)/cyclone
ARM_LDFLAGS    ?= --sysroot=$(ARM_SYSROOT) \
                  -Wl,--dynamic-linker=$(ARM_SYSROOT)/usr/lib/ld-linux-armhf.so.3

QEMU           ?= qemu-arm
QEMU_CPU       ?= cortex-a7
QEMU_LD_PREFIX ?= $(ARM_SYSROOT)

# The ARM9 build assembles the same Cyclone.s (generated with HAVE_ARMv6=0,
# so it is plain ARMv4T) and compiles the harness for ARMv4T with the
# soft-float toolchain.  The result is static, so it needs no sysroot, and it
# runs on a real ARM946 or under qemu-arm -cpu arm946.
ARM9_CC         ?= arm-linux-gnueabi-gcc
ARM9_ARCH_FLAGS := -marm -march=armv4t -mfloat-abi=soft
ARM9_CFLAGS     ?= -O2 -g -std=gnu11 -Wall -Wextra $(ARM9_ARCH_FLAGS)
ARM9_CPPFLAGS   := -I. -I$(CORE_DIR)/cyclone
ARM9_LDFLAGS    ?= -static
QEMU_CPU_ARM9   ?= arm946

TESTS ?= tests/m68000/v1

.PHONY: all native cyclone arm9 run-fame run-musashi run-cyclone run-arm9 clean

all: native cyclone arm9
native: $(BUILD)/m68k-test-fame $(BUILD)/m68k-test-musashi
cyclone: $(BUILD)/m68k-test-cyclone
arm9: $(BUILD)/m68k-test-arm9

# ---------------------------------------------------------------------------
# FAME (native)
# ---------------------------------------------------------------------------

FAME_DIR  := $(BUILD)/fame
FAME_OBJS := $(FAME_DIR)/m68k_test.o $(FAME_DIR)/core_fame.o $(FAME_DIR)/famec.o

$(FAME_DIR):
	mkdir -p $@

$(FAME_DIR)/m68k_test.o: m68k_test.c core.h | $(FAME_DIR)
	$(NATIVE_CC) $(NATIVE_CFLAGS) $(NATIVE_CPPFLAGS) -I$(CORE_DIR)/fame \
		-DCORE_FAME -c $< -o $@

$(FAME_DIR)/core_fame.o: core_fame.c core.h | $(FAME_DIR)
	$(NATIVE_CC) $(NATIVE_CFLAGS) $(NATIVE_CPPFLAGS) -I$(CORE_DIR)/fame \
		-c $< -o $@

# Build a copy of famec.c with the trace *exception* suppressed: the SST
# vectors deliberately set SR.T but expect the instruction result, not a trace
# exception.  Keep FAMEC_EMULATE_TRACE defined so SR.T is still read and
# written; only the line that schedules the exception is neutralised.
# FAMEC_NO_GOTOS is forced on the command line so the reported PC is always
# valid.
$(FAME_DIR)/famec.c: $(CORE_DIR)/fame/famec.c $(CORE_DIR)/fame/famec_opcodes.h | $(FAME_DIR)
	sed -e 's/\r$$//' \
	    -e '/^#define FAMEC_NO_GOTOS$$/d' \
	    -e 's/ctx->execinfo |= FM68K_EMULATE_TRACE;/\/* trace exception suppressed *\//' $< > $@

$(FAME_DIR)/famec.o: $(FAME_DIR)/famec.c | $(FAME_DIR)
	$(NATIVE_CC) -O2 -g -DFAMEC_NO_GOTOS -I$(ROOT) -I$(CORE_DIR)/fame \
		-c $< -o $@

$(BUILD)/m68k-test-fame: $(FAME_OBJS)
	$(NATIVE_CC) -o $@ $^ $(NATIVE_LDFLAGS)

# ---------------------------------------------------------------------------
# Musashi (native)
# ---------------------------------------------------------------------------

MUSASHI_DIR := $(BUILD)/musashi
MUSASHI_CPPFLAGS := -I. -I$(ROOT) -I$(MUSASHI_DIR) -I$(CORE_DIR)/musashi
MUSASHI_GEN := $(MUSASHI_DIR)/m68kops.c
MUSASHI_HDR := $(MUSASHI_DIR)/m68kops.h
MUSASHI_OBJS := $(MUSASHI_DIR)/m68k_test.o $(MUSASHI_DIR)/core_musashi.o \
                $(MUSASHI_DIR)/m68kcpu.o $(MUSASHI_DIR)/m68kops.o

$(MUSASHI_DIR):
	mkdir -p $@

$(MUSASHI_DIR)/m68kmake: $(CORE_DIR)/musashi/m68kmake.c | $(MUSASHI_DIR)
	$(NATIVE_CC) -O2 -I$(CORE_DIR)/musashi -o $@ $<

# m68kmake opens "m68k_in.c" relative to its working directory and writes
# m68kops.c/.h there.  Keep the generated files in build/musashi/ and point
# the generator at the source tree with a symlink.
$(MUSASHI_DIR)/m68k_in.c: | $(MUSASHI_DIR)
	ln -sf ../../$(CORE_DIR)/musashi/m68k_in.c $@

$(MUSASHI_GEN) $(MUSASHI_HDR): $(MUSASHI_DIR)/m68kmake $(MUSASHI_DIR)/m68k_in.c
	cd $(MUSASHI_DIR) && ./m68kmake

# The SST vectors expect instruction results with SR.T set, not trace
# exceptions.  Use a local copy of the Musashi headers with trace emulation
# disabled.  The copies live in build/musashi/ so the quote-include search
# finds them before cpu/musashi/.
$(MUSASHI_DIR)/m68kconf.h: $(CORE_DIR)/musashi/m68kconf.h | $(MUSASHI_DIR)
	sed 's/^#define M68K_EMULATE_TRACE.*OPT_ON/#define M68K_EMULATE_TRACE          OPT_OFF/' $< > $@

$(MUSASHI_DIR)/m68k.h: $(CORE_DIR)/musashi/m68k.h $(MUSASHI_DIR)/m68kconf.h | $(MUSASHI_DIR)
	cp $< $@

$(MUSASHI_DIR)/m68kcpu.h: $(CORE_DIR)/musashi/m68kcpu.h | $(MUSASHI_DIR)
	cp $< $@

$(MUSASHI_DIR)/m68kcpu.c: $(CORE_DIR)/musashi/m68kcpu.c | $(MUSASHI_DIR)
	cp $< $@

$(MUSASHI_DIR)/m68k_test.o: m68k_test.c core.h | $(MUSASHI_DIR)
	$(NATIVE_CC) $(NATIVE_CFLAGS) $(MUSASHI_CPPFLAGS) -DCORE_MUSASHI \
		-c $< -o $@

$(MUSASHI_DIR)/core_musashi.o: core_musashi.c core.h \
		$(MUSASHI_DIR)/m68kcpu.h $(MUSASHI_DIR)/m68k.h \
		$(MUSASHI_DIR)/m68kconf.h | $(MUSASHI_DIR)
	$(NATIVE_CC) $(NATIVE_CFLAGS) $(MUSASHI_CPPFLAGS) -c $< -o $@

$(MUSASHI_DIR)/m68kcpu.o: $(MUSASHI_DIR)/m68kcpu.c $(MUSASHI_HDR) \
		$(MUSASHI_DIR)/m68kcpu.h $(MUSASHI_DIR)/m68k.h \
		$(MUSASHI_DIR)/m68kconf.h | $(MUSASHI_DIR)
	$(NATIVE_CC) -O2 -g $(MUSASHI_CPPFLAGS) -c $< -o $@

$(MUSASHI_DIR)/m68kops.o: $(MUSASHI_GEN) $(MUSASHI_HDR) \
		$(MUSASHI_DIR)/m68kcpu.h $(MUSASHI_DIR)/m68k.h \
		$(MUSASHI_DIR)/m68kconf.h | $(MUSASHI_DIR)
	$(NATIVE_CC) -O2 -g $(MUSASHI_CPPFLAGS) -c $< -o $@

$(BUILD)/m68k-test-musashi: $(MUSASHI_OBJS)
	$(NATIVE_CC) -o $@ $^ $(NATIVE_LDFLAGS)

# ---------------------------------------------------------------------------
# Cyclone (32-bit ARM, hard-float sysroot, run under qemu-arm)
# ---------------------------------------------------------------------------

CYCLONE_DIR := $(BUILD)/cyclone
CYCLONE_GEN_DIR := $(CYCLONE_DIR)/gen
CYCLONE_GEN := $(CYCLONE_DIR)/cyclone_gen
CYCLONE_CXX_SRC := Main Ea OpAny OpArith OpBranch OpLogic OpMove
CYCLONE_GEN_OBJS := $(addprefix $(CYCLONE_GEN_DIR)/,$(addsuffix .o,$(CYCLONE_CXX_SRC))) \
                    $(CYCLONE_GEN_DIR)/Disa.o
CYCLONE_OBJS := $(CYCLONE_DIR)/m68k_test.o $(CYCLONE_DIR)/core_cyclone.o \
                $(CYCLONE_DIR)/Cyclone.o

$(CYCLONE_DIR) $(CYCLONE_GEN_DIR):
	mkdir -p $@

$(CYCLONE_GEN_DIR)/%.o: $(CORE_DIR)/cyclone/%.cpp | $(CYCLONE_GEN_DIR)
	$(CXX) -O2 -g -DHAVE_ARMv6=0 -I$(CORE_DIR)/cyclone -c $< -o $@

$(CYCLONE_GEN_DIR)/Disa.o: $(CORE_DIR)/cyclone/Disa/Disa.c | $(CYCLONE_GEN_DIR)
	$(NATIVE_CC) -O2 -g -DHAVE_ARMv6=0 -I$(CORE_DIR)/cyclone -c $< -o $@

$(CYCLONE_GEN): $(CYCLONE_GEN_OBJS)
	$(CXX) -o $@ $^

$(CYCLONE_DIR)/Cyclone.s: $(CYCLONE_GEN)
	cd $(CYCLONE_DIR) && ./cyclone_gen

$(CYCLONE_DIR)/Cyclone.o: $(CYCLONE_DIR)/Cyclone.s | $(CYCLONE_DIR)
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

$(CYCLONE_DIR)/m68k_test.o: m68k_test.c core.h | $(CYCLONE_DIR)
	$(ARM_CC) $(ARM_CFLAGS) $(ARM_CPPFLAGS) -DCORE_CYCLONE -c $< -o $@

$(CYCLONE_DIR)/core_cyclone.o: core_cyclone.c core.h | $(CYCLONE_DIR)
	$(ARM_CC) $(ARM_CFLAGS) $(ARM_CPPFLAGS) -c $< -o $@

$(BUILD)/m68k-test-cyclone: $(CYCLONE_OBJS)
	$(ARM_CC) $(ARM_CFLAGS) $(ARM_LDFLAGS) -o $@ $^

# ---------------------------------------------------------------------------
# Cyclone for ARM9 (32-bit ARMv4T, soft-float, static, run under qemu-arm)
# ---------------------------------------------------------------------------

ARM9_DIR  := $(BUILD)/arm9
ARM9_OBJS := $(ARM9_DIR)/m68k_test.o $(ARM9_DIR)/core_cyclone.o \
             $(ARM9_DIR)/Cyclone.o

$(ARM9_DIR):
	mkdir -p $@

$(ARM9_DIR)/Cyclone.o: $(CYCLONE_DIR)/Cyclone.s | $(ARM9_DIR)
	$(ARM9_CC) $(ARM9_CFLAGS) -c $< -o $@

$(ARM9_DIR)/m68k_test.o: m68k_test.c core.h | $(ARM9_DIR)
	$(ARM9_CC) $(ARM9_CFLAGS) $(ARM9_CPPFLAGS) -DCORE_CYCLONE -c $< -o $@

$(ARM9_DIR)/core_cyclone.o: core_cyclone.c core.h | $(ARM9_DIR)
	$(ARM9_CC) $(ARM9_CFLAGS) $(ARM9_CPPFLAGS) -c $< -o $@

$(BUILD)/m68k-test-arm9: $(ARM9_OBJS)
	$(ARM9_CC) $(ARM9_CFLAGS) $(ARM9_LDFLAGS) -o $@ $^

# ---------------------------------------------------------------------------
# Convenience targets
# ---------------------------------------------------------------------------

run-fame: $(BUILD)/m68k-test-fame
	$(BUILD)/m68k-test-fame $(TESTS)

run-musashi: $(BUILD)/m68k-test-musashi
	$(BUILD)/m68k-test-musashi $(TESTS)

run-cyclone: $(BUILD)/m68k-test-cyclone
	QEMU_LD_PREFIX=$(QEMU_LD_PREFIX) $(QEMU) -cpu $(QEMU_CPU) \
		$(BUILD)/m68k-test-cyclone $(TESTS)

run-arm9: $(BUILD)/m68k-test-arm9
	$(QEMU) -cpu $(QEMU_CPU_ARM9) $(BUILD)/m68k-test-arm9 $(TESTS)

clean:
	rm -rf $(BUILD)
