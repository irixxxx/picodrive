#!/bin/sh
# Fetch SingleStepTests/m68000 v1 binary test files.
#
# Usage:
#   ./fetch-tests.sh              # representative smoke set
#   ./fetch-tests.sh --all        # all 127 files
#   ./fetch-tests.sh --force      # re-download existing files
#   ./fetch-tests.sh --list       # list remote file names
#
# Tests are stored in tests/m68000/v1/ (git-ignored).

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEST="$SCRIPT_DIR/tests/m68000/v1"
API_URL="https://api.github.com/repos/SingleStepTests/m68000/contents/v1"
RAW_BASE="https://raw.githubusercontent.com/SingleStepTests/m68000/main/v1"

# Representative subset: ALU, shifts/rotates, branches, memory, exceptions,
# privilege, and the STOP instruction.
SMOKE="
ADD.b.json.bin ADD.w.json.bin ADD.l.json.bin
ADDX.b.json.bin
AND.w.json.bin
ASL.w.json.bin ASR.w.json.bin
Bcc.json.bin BSR.json.bin
BTST.json.bin
CLR.w.json.bin
CMP.w.json.bin
DBcc.json.bin
DIVS.json.bin DIVU.json.bin
EOR.w.json.bin
EXT.w.json.bin
JMP.json.bin JSR.json.bin
LEA.json.bin LINK.json.bin
LSL.w.json.bin LSR.w.json.bin
MOVE.b.json.bin MOVE.w.json.bin MOVE.l.json.bin
MOVEA.w.json.bin
MOVEM.w.json.bin
MULS.json.bin MULU.json.bin
NEG.w.json.bin
NOP.json.bin
NOT.w.json.bin
OR.w.json.bin
PEA.json.bin
ROL.b.json.bin ROR.b.json.bin
RTE.json.bin RTR.json.bin RTS.json.bin
Scc.json.bin
STOP.json.bin
SUB.w.json.bin
SWAP.json.bin
TAS.json.bin
TRAP.json.bin TRAPV.json.bin
TST.w.json.bin
"

MODE=smoke
FORCE=0

usage() {
    sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
}

for arg in "$@"; do
    case "$arg" in
        --all)   MODE=all ;;
        --smoke) MODE=smoke ;;
        --force) FORCE=1 ;;
        --list)  MODE=list ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

mkdir -p "$DEST"

if [ "$MODE" = list ]; then
    curl -fsSL "$API_URL" | python3 -c '
import json, sys
for entry in json.load(sys.stdin):
    print(entry["name"])
'
    exit 0
fi

if [ "$MODE" = all ]; then
    NAMES=$(curl -fsSL "$API_URL" | python3 -c '
import json, sys
for entry in json.load(sys.stdin):
    print(entry["name"])
')
else
    NAMES=$SMOKE
fi

for name in $NAMES; do
    out="$DEST/$name"
    if [ -f "$out" ] && [ "$FORCE" != 1 ]; then
        echo "skip   $name"
        continue
    fi
    echo "fetch  $name"
    curl -fsSL -o "$out.tmp" "$RAW_BASE/$name"
    mv "$out.tmp" "$out"
done

count=$(find "$DEST" -maxdepth 1 -name '*.json.bin' | wc -l | tr -d ' ')
echo "done: $count test files in $DEST"
