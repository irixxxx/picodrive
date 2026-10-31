# platform/gp2x/test940

Host-side regression tests for the GP2X 940 sound path.

The default target tests the 920-side 940 policy in `PsndRerate()` and the
920 half of the driver in `platform/gp2x/940ctl.c`.  The optional
`host-core` target compiles the real 940 C job loop and the real
YM2612/YM2413 cores with small host stubs.

Neither target needs the GP2X toolchain, the 940 image, or real hardware.

## Run

```sh
make -C platform/gp2x/test940              # 920-side rerate + 940 control test
make -C platform/gp2x/test940 host-core    # 940 C job layer
```

The rerate binary is written to
`platform/gp2x/test940/build/fallback_test`, the control test to
`platform/gp2x/test940/build/ctl_test`, and the host-core binary to
`platform/gp2x/test940/build-host/host_core_test`; all targets run their
binary automatically.  Use `make -C platform/gp2x/test940 clean` to remove
both build directories.

## Rerate cases

The default target compiles the real `pico/sound/sound.c` with `__GP2X__` and
replaces the hardware-facing 940 API and the local sound cores with
recording stubs.

The tests assert the policy that there is no automatic switch from the 940
to the 920.  A failed or crashed 940 stays selected and silent until the
user turns the option off or the 940 is successfully restarted.

- MD boot: `YM2612Init_940()` fails, the local YM2612 is not initialized,
  `POPT_EXT_FM` stays set, and FM updates still use the 940 wrapper (which
  returns silence after a failure).
- MD mid-game: the same failure with no local init and no fallback.  The
  sound layer does not save/load its own state for the 940 path; state
  handover is owned by `YM2612Init_940()`.
- MD/SMS option off/on: turning the 940 off hands the chip back to the 920;
  turning it on reinitializes the 940 even when the sound parameters did not
  change and hands the local state over (with `preserve_state` for SMS).
- MD crash recovery: the menu-exit rerate always calls `YM2612Init_940()`;
  its cache skips an unchanged live setup and its crash check reloads the
  image.
- MD failed crash recovery: a reload that still fails does not retry with
  the local core; the 940 remains selected and the next menu exit tries
  again.
- SMS boot/mid-game failure: the local OPLL is not selected and rendering
  stays on the 940 wrapper.

## Control cases

`ctl_test.c` includes the real `platform/gp2x/940ctl.c` with its shared
pointers redirected to process memory and stubs for the GP2X MMIO, menu and
sound-core entry points.  It covers:

- the YM2612 init cache: clock, rate and SSG/DAC flags all trigger a
  reinitialization, an unchanged setup does not
- a failed image init: the option is kept, the 940 stays the owner, the
  render paths return silence, and a second init reloads the image instead
  of selecting the local core
- first-enable handover for both chips while `crashed_940` still marks the
  unloaded image; the local chip is authoritative when the 940 has no owner
- a stale image is rejected through the buffer descriptors published by the
  940; a zero, short or out-of-range descriptor keeps the 940 selected and
  silent, and no local state is salvaged
- crash handling: `wait_busy_940()` does not spin or try to interrupt the
  dead core, both render paths return silence, and the YM2612 state save
  does not post a job to a dead core.  A crash during handover discards the
  half-packed state and restarts the 940 fresh; nothing is reconstructed
  from the 920 register shadow.
- turning the option off after a YM2612/YM2413 crash resets the local chip
  instead of reconstructing state from the dead core; a later option-on
  hands the fresh local state over normally
- YM2413 render requests larger than `YM2413_BUF_MAX` are rendered in
  blocks instead of being truncated
- a clock/rate change applies the queue and packs the live 940 state
  before restarting the chip, not the stale local shadow
- a full YM2413 write queue is flushed with a zero-length update instead of
  dropping writes, and the terminator is still written
- handing the YM2413/YM2612 back to the 920 resets the queue and init cache
- the YM2413 only counts as 940-owned for SMS hardware, not for VGM

## Host-core cases

`host_core_940.c` includes the real
`platform/gp2x/code940/940.c` and redirects its fixed shared-memory
pointers to normal process memory.  `host_core_test.c` supplies stubs for
the ARM940 startup/IRQ/cache/MMIO helpers and for the Helix MP3 entry
points, then feeds `Main940()` a sequence of jobs.  A minimal Helix header
under `host_include/` keeps this independent of the external Helix sources.

The test covers:

- the buffer descriptors published by the 940 (offset and byte length for
  the YM2612, MP3 and YM2413 buffers)
- `JOB940_INITALL`, including the YM2612 clock/rate/flags and MP3 paths
- a `JOB940_YM2612UPDATEONE` render
- YM2413 reset, register-write queue replay and render
- YM2413 pack/unpack round-trip, including a changed register
- YM2612 legacy pack/load key state, restoring `slot_mask` and per-slot
  keys from the packed `keyon_field`
- a zero-length YM2413 update still applies queued writes
- update lengths, using sentinels just past the rendered samples

The host uses 64-bit pointers, so its reported offsets are not the ARM
layout; this test checks internal consistency, not the 920/940 ABI.

## Scope

The rerate test verifies the 920-side call order and option/state handling.
The control test verifies the 920-side queue, init, crash and hand-over
logic.  The host-core test verifies the C job protocol, buffer handling and
YM2413 state packing.  None executes ARM940 code or exercises the real
shared-memory RPC, job completion, cache coherency, IRQ timing, or 920/940
races.

For the real 940 path, put a deliberately stale `pico940_v4.bin` on the
card (for example, an old v3 image renamed to v4).  The 940 must not start:
audio stays silent and the menu reports the bad image.  The buffer
descriptors are validated, so an image built from different shared-data
headers cannot make the 920 read the wrong memory.  Turning "Use second CPU
for sound" off returns to the local cores until the image is rebuilt.

`pico940_v4.bin` is tracked; rebuild it from the same tree whenever
`940shared.h`, `940.c`, or the 940 sound sources change:

```sh
make -C platform/gp2x/code940
```
