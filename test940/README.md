# test940

Host-side regression tests for the GP2X 940 sound path.

The default target tests the 920-side fallback in `PsndRerate()`.  The
optional `host-core` target compiles the real 940 C job loop and the real
YM2612/YM2413 cores with small host stubs.

Neither target needs the GP2X toolchain, the 940 image, or real hardware.

## Run

```sh
make -C test940              # 920-side fallback test
make -C test940 host-core    # 940 C job layer
```

The fallback binary is written to `test940/build/fallback_test`, and the
host-core binary to `test940/build-host/host_core_test`; both targets run
their binary automatically.  Use `make -C test940 clean` to remove both
build directories.

## Fallback cases

The default target compiles the real `pico/sound/sound.c` with `__GP2X__` and
replaces the hardware-facing 940 API and the local sound cores with
recording stubs.

- MD boot: `YM2612Init_940()` fails, the option is cleared, and the local
  YM2612 init runs.
- MD internal-clear regression: the 940 init clears `POPT_EXT_FM` itself and
  returns failure; the local YM2612 init must still run.
- MD mid-game: `preserve_state` saves the YM2612 state once and restores it
  once after the fallback.
- SMS boot: `YM2413Init_940()` fails, the local OPLL path is selected, and
  the FIR resampler is recreated.
- SMS mid-game: the same fallback with state preservation.

## Host-core cases

`host_core_940.c` includes the real
`platform/gp2x/code940/940.c` and redirects its fixed shared-memory
pointers to normal process memory.  `host_core_test.c` supplies stubs for
the ARM940 startup/IRQ/cache/MMIO helpers and for the Helix MP3 entry
points, then feeds `Main940()` a sequence of jobs.  A minimal Helix header
under `host_include/` keeps this independent of the external Helix sources.

The test covers:

- the shared layout magic and published offsets
- `JOB940_INITALL`, including the YM2612 and MP3 init paths
- a `JOB940_YM2612UPDATEONE` render
- YM2413 reset, register-write queue replay and render
- YM2413 state pack/unpack
- update lengths, using sentinels just past the rendered samples

The host uses 64-bit pointers, so its reported offsets are not the ARM
layout; this test checks internal consistency, not the 920/940 ABI.

## Scope

The fallback test verifies the 920-side call order and option/state
handling.  The host-core test verifies the C job protocol, buffer handling
and YM2413 state packing.  Neither executes ARM940 code or exercises the
real shared-memory RPC, job completion, cache coherency, IRQ timing, or
920/940 races.

For the real 940 path, put a deliberately stale `pico940_v4.bin` on the
card (for example, an old v3 image renamed to v4) and verify that the log
reports the layout mismatch while MD and SMS audio continue to work through
the local cores.
