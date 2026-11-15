# 68000 single-step test harness

Runs the [SingleStepTests/m68000](https://github.com/SingleStepTests/m68000)
v1 vectors against the three 68000 cores PicoDrive supports: Cyclone
(`EMU_C68K`), Musashi (`EMU_M68K`) and FAME (`EMU_F68K`).

The harness is built outside the emulator, so a core can be checked without
building PicoDrive. Every vector is a single instruction: it supplies the
initial registers, SR, PC and memory, and the harness compares the
architectural state afterwards.

## Prerequisites

FAME and Musashi are plain C and build with the native compiler. The two
Cyclone binaries are 32-bit ARM, so they need more than a normal desktop
install has:

- `arm-linux-gnueabihf-gcc` plus a hard-float sysroot for the ARMv7 build,
  and `arm-linux-gnueabi-gcc` for the static ARM9 build;
- `qemu-arm` to run those two binaries.

The supported way to get all of it is a single docker container with the cross
toolchains and `qemu-arm` installed. `run.sh` builds and runs inside such a
container when `CONTAINER` is set, and nothing else is required from it; a
stopped one is started first. Leaving `CONTAINER` unset runs the same commands
on the host, which then needs those toolchains and GNU `sed` itself (the
Makefile copies the cores with `sed`, so a BSD one fails on the CRLF sources).
The hard-float sysroot is expected at
`$HOME/opt/linux-cross/arm-linux-gnueabihf`, which `ARM_SYSROOT` overrides if
the toolchain lives elsewhere.

## Binaries

| core | PicoDrive name | source | binary | runs on |
| --- | --- | --- | --- | --- |
| Cyclone (ARMv7) | `EMU_C68K` | `cpu/cyclone` (generated ARM asm) | `build/m68k-test-cyclone` | `qemu-arm -cpu cortex-a7` |
| Cyclone (ARM9) | `EMU_C68K` | same `Cyclone.s`, ARMv4T | `build/m68k-test-arm9` | `qemu-arm -cpu arm946` |
| Musashi | `EMU_M68K` | `cpu/musashi` (C) | `build/m68k-test-musashi` | native aarch64 |
| FAME | `EMU_F68K` | `cpu/fame` (C) | `build/m68k-test-fame` | native aarch64 |

The FAME and Musashi binaries are native; the two Cyclone binaries are 32-bit
ARM because `struct Cyclone.membase` is an `int`, and they run under
`qemu-arm`.

## Test data

`fetch-tests.sh` downloads the binary vectors into `tests/m68000/v1/`, which
is git-ignored. The vectors are not in the repo and are not a submodule, so a
checkout pulls nothing and needs no network to build:

```sh
./fetch-tests.sh            # 48-file smoke set (120,000 vectors)
./fetch-tests.sh --all      # all 127 files (317,500 vectors)
./fetch-tests.sh --force    # re-download existing files
./fetch-tests.sh --list     # list remote file names
```

## Building and running

```sh
make                        # all four binaries
make native                 # FAME + Musashi only
make run-fame               # build and run one core
make run-musashi
make run-cyclone
make run-arm9
make clean
```

`run.sh` does the same, on the host or in the container:

```sh
./run.sh                              # all cores, full test directory
./run.sh --core=fame                  # one core
./run.sh --tests=tests/m68000/v1/ADD.w.json.bin -- --verbose
./run.sh --build-only
```

It takes its configuration from the environment, so no checkout location is
baked in:

| variable | meaning |
| --- | --- |
| `CONTAINER` | docker container to run in; unset runs on the host |
| `CONT_REPO` | checkout path inside the container, required with `CONTAINER` |
| `CONTAINER_USER` | `docker exec` user, default the container's own |
| `CONTAINER_HOME` | `HOME` inside the container, default read from it |
| `ARM_SYSROOT` | hard-float sysroot (see above) |
| `QEMU`, `QEMU_CPU` | qemu binary and `-cpu` for the ARMv7 build |

```sh
CONTAINER=builder CONT_REPO=/src/picodrive ./run.sh
```

`--tests` is a path inside the container when `CONTAINER` is set, and a path
on the host otherwise; a relative one is relative to this directory.

Running a binary directly works too; it defaults to `tests/m68000/v1`:

```sh
./build/m68k-test-fame tests/m68000/v1 --max-fail=0
QEMU_LD_PREFIX=$HOME/opt/linux-cross/arm-linux-gnueabihf \
  qemu-arm -cpu cortex-a7 ./build/m68k-test-cyclone tests/m68000/v1
qemu-arm -cpu arm946 ./build/m68k-test-arm9 tests/m68000/v1
```

The ARMv7 Cyclone build needs the hard-float sysroot under
`$HOME/opt/linux-cross/arm-linux-gnueabihf`; the ARM9 build is static and
needs no sysroot.

## Options

| option | meaning |
| --- | --- |
| `-q`, `--quiet` | only print the final summary |
| `-v`, `--verbose` | print every failure/warning (default: first 20) |
| `--cycles` | treat cycle-count mismatches as failures |
| `--address-errors` | run address-error tests instead of skipping |
| `--max-fail=N` | stop after N failures (0 = unlimited) |
| `--filter=SUBSTR` | only run tests whose name contains SUBSTR |
| `--list` | list test names without executing them |
| `-h`, `--help` | show help |

Each file is one instruction family (for example `DIVU.json.bin`), so a run
reports per file and then per core.

## Results

Full 127-file run (317,500 vectors, 2,500 per file), 2026-11-14:

| core | passed | skipped | failed | warnings |
| --- | ---: | ---: | ---: | ---: |
| Cyclone (ARMv7) | 261,894 | 55,606 | 0 | 0 |
| Cyclone (ARM9) | 261,894 | 55,606 | 0 | 0 |
| Musashi | 261,894 | 55,606 | 0 | 0 |
| FAME | 260,816 | 55,606 | 1,078 | 0 |

For reference, the 48-file smoke set gave:

| core | passed | skipped | failed | warnings |
| --- | ---: | ---: | ---: | ---: |
| Cyclone (ARMv7) | 91,158 | 28,842 | 0 | 0 |
| Cyclone (ARM9) | 91,158 | 28,842 | 0 | 0 |
| Musashi | 91,158 | 28,842 | 0 | 0 |
| FAME | 90,080 | 28,842 | 1,078 | 0 |

The Cyclone numbers are from the current `cpu/cyclone` working tree; an older
generated `Cyclone.s` failed 2,145 vectors (`LINK`, `CHK`, `DIVS`, `DIVU`).
Both Cyclone builds produce identical results, so assembling `Cyclone.s` for
ARMv4T does not change the core's behaviour on any vector.

Cyclone and Musashi pass every vector. FAME's only failures are the `TAS`
write-back that PicoDrive deliberately suppresses on the main CPU, listed below
so a future run can be compared against this baseline.

### Per-file failures (full set)

| file | Cyclone (ARMv7/ARM9) | Musashi | FAME |
| --- | ---: | ---: | ---: |
| `TAS` | - | - | 1,078 |

Cyclone fails none. Every other file in the 127-file set passes on every core
(excluding the address-error tests skipped by default).

### Known divergences

These failures are **not** harness bugs. They are differences between the
cores (as configured by PicoDrive) and the reference implementation used to
generate the SST vectors.

- **FAME `TAS` (main CPU).** PicoDrive's `PICODRIVE_HACK` performs the TAS
  write-back only when `ctx == &PicoCpuFS68k`, i.e. for the secondary FAME
  CPU. On the main CPU the write-back is suppressed, so the SST vectors (which
  expect the standard 68000 read-modify-write) report 1,078 failures. This is
  a deliberate PicoDrive configuration difference, not a harness bug.
- **Cycle warnings.** None. Cyclone, Musashi and FAME all report the
  reference cycle count on every vector, exception, `CHK`, divide and
  branch-to-self path included.

### Notes on the build

- Cyclone's C side **must** be compiled with `-marm`. This toolchain defaults
  to Thumb, and a Thumb `BL` into the ARM `CycloneInitJT`/`CycloneResetJT`
  entry points is decoded as garbage and dies with an illegal instruction
  under `qemu-arm`. The Makefile puts `-marm` in `ARM_ARCH_FLAGS`.
- The harness memory uses PicoDrive's native layout: host-endian 16-bit
  words, so the 68000 byte at address `a` lives at `memory[a ^ 1]` on a
  little-endian host. FAME's instruction fetch reads a native `u16` directly
  from `Fetch[]`, so a plain big-endian byte buffer produces the wrong
  opcodes. Cyclone and Musashi access memory through the `mem_*` helpers,
  which hide the swap.
- FAME is built from a generated copy of `famec.c` with `FAMEC_NO_GOTOS`
  forced on. The copy keeps `FAMEC_EMULATE_TRACE` defined (so SR.T is still
  read and written) but neutralises the single line that schedules the trace
  exception: the SST vectors set SR.T and expect the instruction result, not
  a trace exception.
- Musashi is built from copies of `m68kconf.h`, `m68k.h`, `m68kcpu.h` and
  `m68kcpu.c` in `build/musashi/` with `M68K_EMULATE_TRACE` set to `OPT_OFF`.
  `m68k.h` is an explicit prerequisite because it is the header that pulls in
  `m68kconf.h`; without it the quote-include search silently falls back to the
  original `OPT_ON` configuration.

## Core notes

### Cyclone

- `struct Cyclone.membase` is an `int`, so the binary must be 32-bit ARM.
- The C side is compiled with `-marm` (see above) so calls into the ARM
  assembly stay in ARM state.
- The adapter applies the same PC conventions as the original tester:
  `pc` from the test is `m_au` (address + 4); when the core reports the
  stopped state, `get_state()` subtracts 4.
- The active A7 is `cpu.a[7]`; the other stack pointer is `cpu.osp`.
- `CycloneRun()` is called with `cpu.cycles = 0`; the cycles used are
  `-cpu.cycles`.

### FAME

- All 256 `ctx.Fetch[]` banks point at the flat 16 MB `memory[]` buffer.
- The active A7 is `ctx.areg[7].D`; the other stack pointer is `ctx.asp`.
- `fm68k_reset()` is called before each test, then the architectural state is
  applied. `fm68k_emulate(&ctx, 1, fm68k_reason_emulate)` executes one
  instruction.
- FAME's `STOP` ends the slice by setting `ctx->io_cycle_counter` to -4
  (SR.T set) or -3, which reports the 4 cycles the vectors expect.
- Cycle costs are unconditional: the `USE_CYCLONE_TIMING[_DIV]` switches and
  their fixed-cycle fallbacks are gone.
- `DIVU`/`DIVS` count the non-restoring divide loop's penalty pairs the way
  Cyclone does, and `CHK` charges its extra pair on the negative trap only
  while `src - reg` still fits in the shifted word, so both now match every
  vector.

### Musashi

- Cycles follow Cyclone's model on the 68000: the divide helpers count the
  non-restoring loop's penalty pairs, and the exception, `CHK`, `STOP` and
  branch-to-self paths subtract the opcode base, which the main loop charges
  after the handler. The 010/020/040 cycle branches keep their old estimates.
- The adapter provides all `m68k_read_memory_*`, `m68k_write_memory_*`,
  `m68k_read_immediate_*`, `m68k_read_pcrelative_*` and disassembler stubs.
- `m68k_set_context(&cpu)`, `m68k_set_cpu_type(M68K_CPU_TYPE_68000)` and
  `m68k_init()` are called once. `m68k_pulse_reset()` runs before each test.
- The state is applied through `m68k_set_reg()`: SR first, then D/A, then
  USP/SSP, then PC.
