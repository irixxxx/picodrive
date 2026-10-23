# test940

Host-side regression test for the GP2X 920 -> 940 sound fallback in
`PsndRerate()`.

The test compiles the real `pico/sound/sound.c` with `__GP2X__` and replaces
the hardware-facing 940 API and the local sound cores with recording stubs.
It runs on a normal development machine and does not need the GP2X
toolchain, the 940 image, or real hardware.

## Run

```sh
make -C test940
```

The test binary is written to `test940/build/fallback_test`; `make` runs it
automatically. Use `make -C test940 clean` to remove the build directory.

## Cases

- MD boot: `YM2612Init_940()` fails, the option is cleared, and the local
  YM2612 init runs.
- MD internal-clear regression: the 940 init clears `POPT_EXT_FM` itself and
  returns failure; the local YM2612 init must still run.
- MD mid-game: `preserve_state` saves the YM2612 state once and restores it
  once after the fallback.
- SMS boot: `YM2413Init_940()` fails, the local OPLL path is selected, and
  the FIR resampler is recreated.
- SMS mid-game: the same fallback with state preservation.

## Scope

This verifies the 920-side call order and option/state handling. It does not
execute ARM940 code, exercise shared-memory RPC or job completion, or test
actual audio output. For the real 940 path, put a deliberately stale
`pico940_v4.bin` on the card (for example, an old v3 image renamed to v4)
and verify that the log reports the layout mismatch while MD and SMS audio
continue to work through the local cores.
