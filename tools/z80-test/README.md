# z80 single-step test harness

Runs the [SingleStepTests/z80](https://github.com/SingleStepTests/z80) v1
vectors against both z80 cores PicoDrive can build with:

- **CZ80** (`cpu/cz80`, `_USE_CZ80`), the portable C core used on every
  non-ARM-assembly target;
- **DrZ80** (`cpu/DrZ80`, `_USE_DRZ80`), the 32-bit ARM assembly core.

The harness lives outside the emulator build and compiles the core sources
directly, so it can be run on a workstation.

## Layout

    z80_test.c        runner: reads vectors, applies state, compares results
    core.h            shared state/memory definitions
    core_cz80.c       CZ80 adapter (registers, memory and port callbacks)
    core_drz80.c      DrZ80 adapter (xmap tables, register layout, ports)
    prepare-tests.py  fetches the v1 json files with git and packs them for the runner
    Makefile          builds build/z80-test-cz80 and build/z80-test-drz80
    run.sh            builds and runs either core, on the host or in a container
    rcheck/           three hand-built vectors for the R transfers

Only that is in the repo: `tests/` is fetched by `prepare-tests.py` and
`build/` is made by `make`, both git-ignored. There is no submodule, so a
checkout neither pulls the vectors nor needs network access to build. The
fetcher uses a shallow, blobless git clone (a few hundred kB of tree metadata;
the selected blobs are downloaded on demand), which avoids the GitHub contents
API's 1000-entry listing cap that used to truncate v1. Fetching needs `git`;
the clone lives under the git-ignored `tests/` directory.

## Prerequisites

CZ80 is plain C and builds with the native compiler; `make` needs nothing
else. DrZ80 is 32-bit ARM assembly, so that half of the harness also needs
`arm-linux-gnueabihf-gcc` and `qemu-arm`, which a normal desktop install does
not have.

The supported way to get both is a single docker container with the cross
toolchain and `qemu-arm` installed. `run.sh` builds and runs inside such a
container when `CONTAINER` is set, and nothing else is required from it; a
stopped one is started first. Leaving `CONTAINER` unset runs the same commands
on the host, which then needs `arm-linux-gnueabihf-gcc`, `qemu-arm` and GNU
`sed` itself (the Makefile copies the cores with `sed`, so a BSD one fails on
the CRLF sources).

```sh
CONTAINER=builder CONT_REPO=/src/picodrive ./run.sh
./run.sh --core=cz80                  # CZ80 only, native compiler
```

`CONT_REPO` is the checkout path inside the container. `CONTAINER_USER` and
`CONTAINER_HOME` override the user and `HOME` used there, and `NATIVE_CC`,
`ARM_CC` and `ARM_RUN` pick a different compiler or emulator.

## Building and running

    ./prepare-tests.py        # fetch the 47-file smoke set
    make                      # native CZ80 runner
    ./build/z80-test-cz80 tests/z80/v1

    make drz80                # DrZ80 runner (arm cross build)
    qemu-arm ./build/z80-test-drz80 tests/z80/v1

`make run` and `make run-drz80` do the same thing, over the paths in `TESTS`,
which defaults to `tests/z80/v1 rcheck`. DrZ80 is ARM assembly, so it
is compiled with `arm-linux-gnueabihf-gcc` and linked statically; the resulting
binary runs under `qemu-arm` or on a handheld. Override `ARM_CC`, `ARM_CFLAGS`
or `ARM_RUN` to use a different toolchain.

`run.sh` wraps the build and both runs, on the host or in the container:

```sh
./run.sh                              # build and run both cores
./run.sh --core=drz80                 # one core
./run.sh --core=cz80 -- --cycles      # test-runner options after --
./run.sh --build-only
```

Its `CONTAINER`, `CONT_REPO`, `CONTAINER_USER` and `CONTAINER_HOME` work as
described in `tools/m68k-test/README.md`.

The runner takes the same options for both cores:

    -q, --quiet          only print the final summary
    -v, --verbose        print every failure/warning (default: first 20)
        --cycles         treat cycle-count mismatches as failures
        --skip-r         do not compare R (hides R differences)
        --max-fail=N     stop after N failures (0 = unlimited)
        --filter=SUBSTR  only run tests whose name contains SUBSTR
        --list           list test names without executing them

Cycle mismatches are warnings by default, so a run reports instruction results
first and timing separately. `--skip-r` does the same for the refresh counter;
both cores maintain R, so it only hides genuine R differences.

`./prepare-tests.py --all` fetches the whole v1 directory (1604 files) in one
`git archive` and packs it the same way. `--rev=REV` (or `SST_Z80_REV`) pins a
different revision; the results below are from commit `ebe1875` (2026-11-16).

## Results

Whole v1 directory at commit `ebe1875` (2026-11-16), 1604 files, 1,604,000
vectors:

| core | comparison | passed | failed | warnings |
| --- | --- | ---: | ---: | ---: |
| CZ80 | default | 1,592,789 | 11,211 | 0 |
| DrZ80 | default | 1,592,789 | 11,211 | 1,000 |

Both cores model R and cycles. The block-instruction fix also corrected CZ80's
R accounting, so `--skip-r` no longer changes its result; DrZ80's number is
unchanged either way. `--cycles` leaves CZ80 at 11,211 but turns DrZ80's 1,000
`ED 73` cycle warnings into failures (12,211).

Smoke set (47 files, 47,000 vectors): CZ80 44,479 passed, 2,521 failed, 0
warnings; DrZ80 the same.

`rcheck/` holds three vectors built by hand, `00`, `ed 4f` and `ed 5f`. They
predate v1 shipping `ED 4F`/`ED 5F`, but still exercise `LD R,A` and `LD A,R`
in a three-vector default run. Both cores pass all three with matching cycle
counts.

### CZ80

Fixed, verified against the whole v1 set (those vectors all pass now):

- **Refresh register R**: it is a 7-bit counter whose bit 7 is a separate latch
  in `zR2`; the increments now keep bit 7 and wrap at 7 bits, the `DD CB`
  form refreshes it twice instead of three times, `CZ80_R` returns and accepts
  the combined byte, and `LD R,A` masks it the same way.
- **`DD F3` (DI)**: `DI` ended in `goto Cz80_Exec_nocheck`, which fetched
  one more opcode; it now ends the step.
- **`DD 26` / `DD 2E`**: `LD HX,n` / `LD LX,n` cost 7 T-states, not 4.
- **ED block instructions** (8,490 vectors: `INI` plus the repeating
  `LDIR`/`LDDR`/`CPIR`/`CPDR`/`INIR`/`INDR`/`OTIR`/`OTDR`): `INI` now sums
  `((C+1)&0xff)+data` (`IND` keeps `C-1`); the repeating forms overwrite the
  undocumented bits 3/5 from PC bits 11/13 (PC after the two-byte rewind,
  matching Ares/SST), and the port forms apply the repeat-time PV/H
  correction; the leftover `zR--` at the slice-boundary rewind is gone, which
  also fixes the last R mismatch.

Expected: `EI` (`fb`, `dd fb`, `fd fb`) runs the following instruction (the
delay slot) before checking interrupts, so the single-step vectors report PC
and R one instruction ahead. That is the long-standing EI behaviour and is
intentional; `DI` now ends the step.

Still failing:

- **`BIT n,(HL)`** (6,001 across `cb 46`, `cb 4e`, `cb 56`, `cb 5e`, `cb 66`,
  `cb 6e`, `cb 76`, `cb 7e`): these flags come from the high byte of the
  internal WZ/MEMPTR register, which BIT does not modify and which CZ80 does
  not model at all; the address high byte is used as the best available
  approximation (`BIT n,(IX+d)` matches, because there the address *is* WZ).
- **`SCF` / `CCF`** (2,210 across `37`, `3f`, `dd 37`, `dd 3f`, `fd 37`,
  `fd 3f`): these follow Patrik Rak's rule, `YF/XF = ((Q ^ F) | A) & 0x28`,
  where Q is the value the previous instruction wrote into F, or 0 if it wrote
  none; a prefix counts as the previous instruction, so Q is 0 for the DD/FD
  forms. That rule reproduces all 1000 vectors of each of the six files
  (using the `q` byte for the unprefixed ones and Q = 0 for the prefixed
  ones). Modelling Q would mean tracking whether each instruction writes
  flags, a per-instruction cost that is not worth paying for two undocumented
  bits, so CZ80 keeps using A alone, which is only what the vectors expect
  when `Q == F` (about half).

### DrZ80

`R` is maintained: it is bumped on every M1 cycle (the run entry, the tail of
`fetch`, and the `CB` / `ED` / `DD` / `FD` prefix bytes), and `DD CB` / `FD CB`
count 2 rather than 3, as on hardware.  It is packed into the top byte of the
F register as `(count << 1) | bit7`, so the per-M1 update is a single
`add z80f,z80f,#2<<24` that wraps the count and leaves the bit-7 latch alone;
the field has to sit in the top byte, since a byte at bits 8-15 would carry
into bit 16 when the count wraps.  The core unpacks it at the run entry and
stores it back into the context's `Z80R` byte at the run exit.  `LD R,A` and
`LD A,R` convert between the two layouts in their handlers, one `mov
r1,r1,lsl#1` and one `mov r1,r1,lsl#7`; `pico/z80if.c` and this harness apply
the matching rotate pair to that context byte.  The two places that used the
top byte of `z80f` as scratch, the `BIT n,(HL)` WZ stash and the byte the
`LDI`/`LDD` family copies, keep that value in bits 8-15 instead.
Interrupt acknowledge adds its two M1 cycles, which no vector covers.

The ED block repeats now match the vectors too: `LDIR`/`LDDR` and
`CPIR`/`CPDR` take bits 3/5 from PC bits 11/13 when they repeat, and
`INIR`/`INDR`/`OTIR`/`OTDR` additionally apply the repeat-time PV/H correction
(parity of `(B±1)&7`, `H` from the low nibble). The non-repeating forms were
already correct.

`LD A,I` / `LD A,R` (`ED 57` / `ED 5F`) now copy the undocumented YF/XF from
the byte loaded into A (`copyFlag35 z80a`), matching CZ80 and hardware; these
files pass 1000/1000. `LD R,A` (`ED 4F`) was already correct.

Still failing:

- **`BIT n,(HL)`** (6,001 across `cb 46`, `cb 4e`, `cb 56`, `cb 5e`, `cb 66`,
  `cb 6e`, `cb 76`, `cb 7e`): X/Y are taken from bits 3 and 5 of the address
  high byte, the same model CZ80 uses, replacing the `0xc0` constant that was
  forced in for ZEXALL. Both cores now fail exactly the same vectors, because
  these flags really come from the internal WZ/MEMPTR register, which BIT
  leaves untouched and neither core models.
- **`SCF` / `CCF`** (2,210 across the unprefixed, DD and FD files):
  `copyFlag35` takes X/Y from A, so DrZ80 fails the same vectors CZ80 does,
  the ones where the Q rule differs from A alone.
- **`EI` PC** (3,000 across `fb`, `dd fb`, `fd fb`): `EI` deliberately
  executes the following instruction (the delay slot) before checking
  interrupts, so the PC is one instruction ahead of the single-step vector.
  This is the long-standing behaviour and is intentional; `HALT` and `DI` now
  report the plain post-instruction PC.

`ED 73` (`LD (nn),SP`) is cycle-only: DrZ80 charges 16 T-states where the
vectors expect 20, so all 1,000 vectors are warnings by default and failures
under `--cycles`.

The `DD CB` forms that also copy the result to a register are implemented:
168 handlers replace what used to fall through to `opcode_DD_NF2`, and the
`bit` rows reuse the `(ix+d)` handlers because the register field is ignored
there.  The same change charges the 4 T-states of the `DD`/`FD` prefix in the
shared dispatcher instead of leaving it out, so the `DD CB` cycle counts match
too.

## Comparison model

A test passes when all of the following match:

- the eight main registers, the four shadow registers, `ix`, `iy`, `sp`, `pc`,
  `i`, `im`, `iff1` and `iff2`;
- the whole 64K memory after the instruction;
- every port transaction listed by the vector: an `IN` must read the recorded
  port and receive the recorded value, an `OUT` must write the recorded port
  with the recorded value;
- the instruction's T-state count (`--cycles` makes a mismatch a failure);
- `r`, unless `--skip-r` is given.

`wz` (MEMPTR), `q` and the `ei` interrupt-delay latch are present in the test
files and stored in the packed format, but are not compared because neither
core models them. A vector whose only difference is one of those fields is
reported as a pass.

## Packed test format

`prepare-tests.py` writes one `.json.bin` next to every `.json`. All integers
are little endian.

    file:  u32 magic 0x5A383054, u32 test_count
    test:  u32 num_bytes (0), u32 magic 0xABC12368,
           name: u32 num_bytes (0), u32 magic 0x89ABCDEF, u32 len, bytes,
           initial state block,
           final state block,
           bus block
    state: u32 num_bytes, u32 magic 0x01234567,
           u16 af, bc, de, hl, ix, iy, sp, pc, wz,
           u16 af', bc', de', hl',
           u8 i, r, im, iff1, iff2, p, q, ei,
           u32 ram_count, (u16 address, u8 value) * ram_count
    bus:   u32 num_bytes, u32 magic 0x456789AB,
           u32 total_cycles, u32 port_count,
           (u16 port, u8 value, u8 is_write) * port_count

`af` is packed in the usual order (A in the high byte).

## Harness details

- CZ80 is compiled from a copy in `build/cz80/` with `PICODRIVE_HACKS` off
  (`cz80.h` defines it unconditionally). PicoDrive's hack routes memory
  through the banked z80 map; the vectors need plain 64K memory, so the
  adapter supplies the `Read_Byte`/`Write_Byte`/`IN_Port`/`OUT_Port` callbacks
  instead. CZ80 keeps A in the high byte of `AF`, in the same order as the vector
  format, so the adapter passes both pairs through untouched.
- DrZ80 is compiled as PicoDrive ships it: xmap mode, opcode fetches through a
  direct pointer and the rest through page tables. The harness fills all 64
  pages with the start of its flat 64K buffer, which is what the tables encode
  (`(base - page_start) >> 1`). A, I and the register pairs live in the top
  byte/halfword of their words; F is converted with the tables the assembly
  exports (`DrZ80_ARM`, `DrARM_Z80`). R rides in the top byte of that F word
  during a run and the core writes it back to the struct's `Z80R` byte, the
  same field PicoDrive's save code uses, so the adapter only needs that byte.
- One instruction per test: `Cz80_Exec(&cpu, 1)` and `DrZ80Run(&cpu, 1)`. Both
  run a prefixed opcode to completion, so a budget of one is one instruction
  and the T-states are what the call returns.
- CZ80 fetches opcode bytes and immediates by incrementing a host pointer, so
  a fetch that starts at `0xFFFF` reads past the 64K buffer instead of
  wrapping. The harness keeps a spare page at the end and mirrors the first
  256 bytes into it before each test; without that, the `LD SP,nn` vectors at
  `pc == 0xFFFF` fail with a bogus SP. DrZ80 reads through the same buffer, so
  the mirror covers it too.
- The core is initialised once (CZ80's flag tables are expensive to build) and
  every compared register is written for each test.
