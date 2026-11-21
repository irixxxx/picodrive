# sh2 single-step test harness

Runs the [SingleStepTests/sh4](https://github.com/SingleStepTests/sh4) vectors
against both SH-2 cores PicoDrive has: the MAME interpreter it ships in
`cpu/sh2/mame` and the recompiler in `cpu/sh2/compiler.c`.

The vectors were generated from Reicast's SH-4 interpreter, so only the
encodings the SH-2 also implements are run; everything else is skipped.

The harness lives outside the emulator build and compiles the core sources
directly, so it can run on a workstation without building PicoDrive.

## Test data

`prepare-tests.py` downloads the vectors into `tests/sh4/v1/`, which is
git-ignored. It clones the repository once into `tests/sh4/.sst-sh4` as a
shallow, blobless clone, lists the tree with `git ls-tree` and fetches only
the blobs that are missing, so a re-run downloads nothing that is already
there:

    ./prepare-tests.py                    # every vector file (233, about 92 MB)
    ./prepare-tests.py --force            # re-fetch existing files
    ./prepare-tests.py --list             # list remote file names
    SST_SH4_REV=REV ./prepare-tests.py    # fetch from REV (default origin/main)

A full fetch takes one `git archive` (a single pack); a handful of missing
files are fetched one at a time. Upstream stores the vectors in the binary
format directly, so no packing step is needed. There is no submodule, so a
checkout pulls nothing and needs no network to build the harness itself.

## Building and running

    make
    ./build/sh2-test tests/sh4/v1         # interpreter
    ./build/sh2-test-drc tests/sh4/v1     # recompiler

`make run` does the same over `TESTS`, which defaults to `tests/sh4/v1`.
`run.sh` wraps it and picks the backend:

    ./run.sh                              # interpreter, whole set
    ./run.sh --core=drc                   # recompiler, whole set
    ./run.sh --tests=tests/sh4/v1 -- --verbose
    ./run.sh --build-only

Options:

    -q, --quiet          only print the final summary
    -v, --verbose        print every failure/warning (default: first 20)
        --max-fail=N     stop after N failures (0 = unlimited)
        --filter=SUBSTR  only run tests whose name contains SUBSTR
        --list           list test names without executing them

## Cross builds

The recompiler has a backend for arm, aarch64, mips, powerpc, riscv and x86,
and the harness needs no emulator build, so the same set can be run on each of
them: compile it with the target's compiler and run it under qemu-user.

    SYSROOT=${SYSROOT:-/usr/mipsel-linux-gnu}
    make BUILD=build/mipsel NATIVE_CC=mipsel-linux-gnu-gcc
    QEMU_LD_PREFIX=$SYSROOT \
      qemu-mipsel ./build/mipsel/sh2-test-drc tests/sh4/v1

Not every host has an i686 cross compiler, so 32 bit x86 uses the x86_64 one
with -m32. The link rules take no flags of their own, so -m32 and the i686
libgcc have to go into NATIVE_CC. SYSROOT and GCC32 point at the i686 sysroot
and the 32 bit libgcc of the host:

    SYSROOT=${SYSROOT:-/usr/i686-linux-gnu}
    GCC32=${GCC32:-/usr/lib/gcc-cross/i686-linux-gnu/9}
    CC32="x86_64-linux-gnu-gcc -m32 -B$GCC32 -B$SYSROOT/lib -L$SYSROOT/lib"
    make BUILD=build/i386 NATIVE_CC="$CC32" \
      NATIVE_CFLAGS="-O2 -g -std=gnu11 -Wall -Wextra -m32 \
      -I$SYSROOT/include -fno-stack-protector"
    QEMU_LD_PREFIX=$SYSROOT qemu-i386 ./build/i386/sh2-test-drc tests/sh4/v1

`BUILD` has to differ per target, or the last build wins. `NATIVE_CFLAGS`
replaces the default flags outright, so pass the whole set when overriding
them for a 32 bit arm compiler and keep `-marm` in it: gcc for armhf may
default to thumb, and the recompiler emits arm code and calls its helpers by
their address, so a thumb build jumps into the middle of them on the first
block. Without an override the Makefile adds `-marm` for `arm-*` targets.

Whole set on each backend, cross compiled and run under qemu-user:

| host | cache REGs | passed | skipped | failed |
| --- | ---: | ---: | ---: | ---: |
| aarch64 | 6 | 65,619 | 49,000 | 1,881 |
| arm, hardfp and softfp | 4 | 65,619 | 49,000 | 1,881 |
| mipsel | 4 | 65,619 | 49,000 | 1,881 |
| mips64el | 4 | 65,619 | 49,000 | 1,881 |
| powerpc64le | 9 | 65,619 | 49,000 | 1,881 |
| riscv64 | 7 | 65,619 | 49,000 | 1,881 |
| x86_64 | 3 | 65,619 | 49,000 | 1,881 |
| i386 | 0 | 65,619 | 49,000 | 1,881 |

Every backend fails exactly the same 1,881 tests, in the same three groups
below, although the register cache ranges from 3 to 9 slots. i386 has none at
all, because it deliberately keeps SR out of a host register (DRC_SR_REG is
commented out in `cpu/sh2/compiler.h`), so every guest register lives in the
context there.

Running the set on the other backends found two real bugs. `DIV1 Rn,Rn` gave
the wrong result on mips, powerpc and riscv: those backends kept the shifted
value in a temp and read `Rm` before writing `Rn` back, so with `Rm == Rn` they
added the unshifted value. The three `emith_sh2_div1_step` macros now take
`Rm` from the shifted temp when `Rm == Rn`, which is what the other backends
and the interpreter do.

`TAS.B @Rn` was wrong on i386 only, and only where T should be cleared. With
no DRC_SR_REG, every memory access writes SR back to the context and drops the
cached copy. TAS is the only instruction that changes SR between its read and
its write, so the update went into a register the cache no longer owned and the
write's reload put the old value back. The emit path now re-fetches SR after the
read, so it is mapped and dirty again and the write's own writeback carries the
new T.

## Test format

Each 788-byte record holds:

- the state before and after the reference run: R0-R15, the R_ bank, PC, GBR,
  SR, SSR, SPC, VBR, SGR, DBR, MACL, MACH, PR, FPSCR and FPUL;
- four bus cycles with the fetch address, plus the one data read or write the
  instruction made (address, value, access width);
- five instruction words: the first four fetches from the initial PC and one
  word returned for any fetch outside them.

The reference run executes four instructions: a NOP, the instruction under
test, a delay-slot filler and another NOP. A taken branch is followed through
its delay slot and into its target, which the fifth opcode word covers.

## Comparison model

A test passes when all of the following match the vector:

- R0-R15, PC, SR, PR, GBR, VBR, MACH and MACL;
- the data access: address and value must match, and the access width is
  reported as a warning if it differs;
- the expected read or write must actually happen.

The second register bank (`R_`) is not compared: the SH-2 core does not model
SR.RB, and the only instructions that toggle it are in the skipped list below.

Memory is not modelled. Instruction fetches come from the five opcode words,
and the single data access is answered from the vector's record. The
reference keeps data memory and instruction memory apart, so a PC-relative
load that targets the instruction stream returns the recorded data value, not
the opcode word there.

The interpreter runs one instruction per call. A delayed branch runs its
delay slot inside the same call, so the harness counts that slot as the second
instruction (`ppc` moves to the slot address). The core's instruction peeks
for DT/BRA loop detection are answered from the opcode stream so they are not
mistaken for data accesses.

## SH-2 filtering

The vectors are SH-4 vectors, so the instruction under test is checked
against the SH-2 instruction set rather than against whatever the SH-4 core
decoded. The check is a table in `sh2_test.c`; an encoding the SH-2 does not
have is skipped. That covers the FPU, the SH-3/SH-4 control-register
transfers (SSR, SPC, DBR, FPUL, FPSCR, the bank registers), the SH-4 cache
instructions (PREF, MOVCA.L, OCBI/OCBP/OCBWB) and SHAD/SHLD.

The SH-2 disassembler is only used for the mnemonic in the failure report: it
masks out bits 7-4, so it decodes SH-4 control-register forms as if they were
SH-2 instructions (MOVCA.L comes back as BSRF, OCBP as BRAF).

In addition, the four encodings the vector README lists as SH-2 incompatible
are skipped, because their SH-4 behaviour differs from the SH-2:

    0000000000101011  RTE
    0100mmmm00000111  LDC.L @Rm+,SR
    0100mmmm00001110  LDC Rm,SR
    11000011iiiiiiii  TRAPA #imm

Skipped tests are counted, not failed.

## Results

Whole set, 233 files, 116,500 tests:

| core | passed | skipped | failed | warnings |
| --- | ---: | ---: | ---: | ---: |
| MAME interpreter | 67,500 | 49,000 | 0 | 0 |
| recompiler | 65,619 | 49,000 | 1,881 | 0 |

The MAME SH-2 interpreter matches the reference on every SH-2 encoding the
set covers. The recompiler's failures are all in three known areas, listed
under "Recompiler backend" below. The 49,000 skipped tests are the SH-4-only encodings (FPU, cache
and control-register transfers) and the four incompatible encodings above.

The reference is Reicast, not hardware, so a pass means agreement with
Reicast's SH-4 interpreter. Cycle counts are not compared: the four recorded
bus cycles are fetch/access records, not T-state totals.

## Recompiler backend

`core_drc.c` runs the same vectors through `cpu/sh2/compiler.c`, built with
`SH2_DRC_SINGLE_STEP`: a block holds one instruction, or two for a delayed
branch, so a one-cycle budget retires exactly one instruction and the state
can be compared after the same four steps as the interpreter.

The adapter supplies what the 32X bus normally does:

- a fetch window that mirrors the low bits of the guest address space. The
  vectors use random PCs and branch targets, so the window is a mirror rather
  than an offset, and everything outside the four instruction words reads the
  vector's fifth opcode word;
- 128-entry read and write maps (`a >> 25`) whose entries are handlers, so
  every data access goes through the same recorded-access checks as the
  interpreter adapter;
- the `Pico`/`Pico32x`/`Pico32xMem` stubs, the bus handlers and
  `p32x_sh2_get_mem_ptr`.

PC-relative loads are folded into a direct read of the code stream, which
makes no bus access. The value the reference read is mirrored into the window
at the offset the compiler itself uses for that address,
`(pc & (WINDOW_SIZE-1)) + (a - pc)`, and the missing transaction is accepted
for those two encodings; that is the only place the backend's bus model
differs from the interpreter's.

Blocks are cached per PC and the vectors use random PCs, so a block is stale
only when a PC comes back with different code. The adapter flushes the tcache
when that happens instead of after every vector.

`SH2_TEST_TRACE=1` prints the PC, opcode and state after every step.

### Known differences

| tests | why |
| ---: | --- |
| 866 | `STC SR,Rn` and `STC.L SR,@-Rn`: the recompiler keeps its cycle budget in SR bits 12-31, so reading SR gives 0 there. The vectors' reference model carries the random initial value in bits the SH-2 does not define. |
| 1,011 | `JMP`, `JSR`, `BRAF`, `BSRF` with an odd target: the reference model keeps PC bit 0, the recompiler clears it, which is what the SH-2 does by ignoring that bit. |
| 4 | `MOV.W`/`MOV.L @(disp,PC)` with `disp=0`, where the literal address is one of the four instruction words. The reference keeps data and instruction memory apart, so it holds a data value there that cannot coexist with the opcode word a single-bus core reads; the recompiler returns that opcode word. |

Everything else, including all conditional and unconditional branches, their
delay slots, literals, and the full register and MACH/MACL/PR/SR state,
matches the interpreter. Running the same vectors through both cores also
found one real recompiler bug, `XTRCT Rm,Rn` with `Rm == Rn`, which is fixed
in `cpu/sh2/compiler.c`.
