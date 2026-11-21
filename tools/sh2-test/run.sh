#!/bin/sh
# Build and run the SH-2 single-step harness.
#
# Usage:
#   ./run.sh [options] [-- test-options...]
#
# Options:
#   --tests=PATH   test file or directory (default: tests/sh4/v1)
#   --core=NAME    interp (default) or drc
#   --no-build     skip the build step
#   --build-only   build and exit
#   -h, --help     show this help
#
# Examples:
#   ./run.sh
#   ./run.sh --tests=tests/sh4/v1 -- --verbose
#   ./run.sh -- --filter=0000nnnnmmmm0100
#
# Environment:
#   NATIVE_CC   passed on to the Makefile

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

TESTS="tests/sh4/v1"
CORE="interp"
DO_BUILD=1
BUILD_ONLY=0

usage() {
    sed -n '2,/^$/s/^# \{0,1\}//p' "$0"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --tests=*)     TESTS="${1#--tests=}"; shift ;;
        --core=*)      CORE="${1#--core=}"; shift ;;
        --no-build)    DO_BUILD=0; shift ;;
        --build-only)  BUILD_ONLY=1; shift ;;
        -h|--help)     usage; exit 0 ;;
        --)            shift; break ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

cd "$SCRIPT_DIR"

case "$CORE" in
    interp) BIN=./build/sh2-test ;;
    drc)    BIN=./build/sh2-test-drc ;;
    *)
        echo "unknown core: $CORE" >&2
        exit 2
        ;;
esac

if [ "$DO_BUILD" = 1 ]; then
    echo "== building sh2-test ($CORE) =="
    make
fi

if [ "$BUILD_ONLY" = 1 ]; then
    exit 0
fi

exec "$BIN" "$TESTS" "$@"
