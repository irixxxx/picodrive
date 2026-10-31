# Host-side regression tests for the GP2X 940 sound path.
#
# The default target compiles the real pico/sound/sound.c with __GP2X__ and
# links it against recording stubs for the hardware-facing 940 API, and it
# builds a host test around the real platform/gp2x/940ctl.c.  The optional
# host-core target compiles the real 940 C job loop and sound cores with host
# stubs.  No GP2X toolchain, 940 image, or real hardware is needed.
#
# Usage:
#   make -C platform/gp2x/test940
#   make -C platform/gp2x/test940 host-core
#   make -C platform/gp2x/test940 clean

TEST_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
ROOT := $(abspath $(TEST_DIR)/../../..)

CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -D__GP2X__ -D__GPH__ -I$(ROOT)
LDLIBS ?=

BUILD_DIR := $(TEST_DIR)/build
HOST_BUILD_DIR := $(TEST_DIR)/build-host
SOUND_OBJ := $(BUILD_DIR)/sound_gp2x.o
TEST_BIN := $(BUILD_DIR)/fallback_test
CTL_TEST_BIN := $(BUILD_DIR)/ctl_test

.PHONY: all test run host-core clean

all: test

test: run

run: $(TEST_BIN) $(CTL_TEST_BIN)
	$(TEST_BIN)
	$(CTL_TEST_BIN)

$(TEST_BIN): $(TEST_DIR)/fallback_test.c $(SOUND_OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(SOUND_OBJ) $(LDLIBS)

$(CTL_TEST_BIN): $(TEST_DIR)/ctl_test.c $(ROOT)/platform/gp2x/940ctl.c \
		$(ROOT)/platform/gp2x/940ctl.h \
		$(ROOT)/platform/gp2x/code940/940shared.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-format -o $@ $< $(LDLIBS)

$(BUILD_DIR):
	@mkdir -p $@

$(SOUND_OBJ): $(ROOT)/pico/sound/sound.c $(ROOT)/platform/gp2x/940ctl.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-sign-compare -c -o $@ $<

# Compile and run the real 940 C job layer with stubbed ARM/MMIO helpers.
# This target is optional and does not need Docker or the GP2X toolchain;
# the normal test above remains independent of this one.
host-core:
	$(MAKE) -f $(TEST_DIR)/Makefile.host-core

clean:
	rm -rf $(BUILD_DIR) $(HOST_BUILD_DIR)
