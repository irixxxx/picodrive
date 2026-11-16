#!/bin/sh
# Fetch SingleStepTests/m68000 v1 binary test files.
#
# The vectors are pulled with git instead of the GitHub contents API.  A
# shallow, blobless clone keeps just the tree metadata (a couple hundred kB);
# the selected blobs are then downloaded on demand.  That avoids the API's
# 1000-entry listing cap and lets a run be pinned to a revision.
#
# Usage:
#   ./fetch-tests.sh              # representative smoke set
#   ./fetch-tests.sh --all        # all 127 files
#   ./fetch-tests.sh --force      # re-download existing files
#   ./fetch-tests.sh --list       # list remote file names
#   SST_M68K_REV=REV ./fetch-tests.sh   # fetch from REV (default origin/main)
#
# Tests are stored in tests/m68000/v1/ (git-ignored).

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEST="$SCRIPT_DIR/tests/m68000/v1"
CACHE="$SCRIPT_DIR/tests/m68000/.sst-m68000"
REPO_URL="https://github.com/SingleStepTests/m68000.git"
REV="${SST_M68K_REV:-origin/main}"

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
    cat <<'EOF'
Fetch SingleStepTests/m68000 v1 binary test files.

Usage:
  ./fetch-tests.sh              representative smoke set
  ./fetch-tests.sh --all        all files in v1
  ./fetch-tests.sh --smoke      representative smoke set (default)
  ./fetch-tests.sh --force      re-download existing files
  ./fetch-tests.sh --list       list remote file names

SST_M68K_REV selects the revision to fetch (default origin/main).
EOF
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

ensure_cache() {
    if [ ! -d "$CACHE/.git" ]; then
        mkdir -p "$(dirname -- "$CACHE")"
        echo "cloning $REPO_URL into $CACHE" >&2
        git clone --quiet --depth 1 --filter=blob:none --no-checkout \
            "$REPO_URL" "$CACHE"
    else
        git -C "$CACHE" fetch --quiet --depth 1 origin
    fi
    if [ "$REV" != "origin/main" ]; then
        if ! git -C "$CACHE" rev-parse --verify --quiet "$REV^{commit}" \
                >/dev/null 2>&1; then
            git -C "$CACHE" fetch --quiet --depth 1 origin "$REV" || true
        fi
    fi
}

list_remote() {
    git -C "$CACHE" ls-tree -r --name-only "$REV" v1 | sed 's|^v1/||'
}

ensure_cache

if [ "$MODE" = list ]; then
    list_remote
    exit 0
fi

if [ "$MODE" = all ]; then
    NAMES=$(list_remote)
else
    NAMES=$SMOKE
fi

TODO=""
for name in $NAMES; do
    if [ -f "$DEST/$name" ] && [ "$FORCE" != 1 ]; then
        echo "skip   $name"
    else
        TODO="$TODO $name"
    fi
done

if [ -n "$TODO" ]; then
    if [ "$MODE" = all ]; then
        # One archive downloads the whole blob set in a single pack.
        TMP="$SCRIPT_DIR/tests/m68000/.fetch.$$"
        mkdir -p "$TMP"
        set --
        for name in $TODO; do set -- "$@" "v1/$name"; done
        git -C "$CACHE" archive --format=tar "$REV" "$@" \
            | tar -x -C "$TMP" --strip-components=1
        for name in $TODO; do
            echo "fetch  $name"
            mv "$TMP/$name" "$DEST/$name"
        done
        rm -rf "$TMP"
    else
        # Per-file: only these blobs are downloaded.
        for name in $TODO; do
            echo "fetch  $name"
            git -C "$CACHE" show "$REV:v1/$name" > "$DEST/$name.tmp"
            mv "$DEST/$name.tmp" "$DEST/$name"
        done
    fi
fi

count=$(find "$DEST" -maxdepth 1 -name '*.json.bin' | wc -l | tr -d ' ')
echo "done: $count test files in $DEST"
