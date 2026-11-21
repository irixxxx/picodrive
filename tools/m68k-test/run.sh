#!/bin/sh
# Build and run the m68k single-step harness.
#
# Usage:
#   ./run.sh [options] [-- test-options...]
#
# Options:
#   --core=all|fame|musashi|cyclone|arm9
#                                     core(s) to run (default: all)
#   --tests=PATH                      test file or directory (default:
#                                     tests/m68000/v1)
#   --no-build                        skip the build step
#   --build-only                      build and exit
#   -h, --help                        show this help
#
# Examples:
#   ./run.sh
#   ./run.sh --core=cyclone -- --verbose
#   ./run.sh --tests=tests/m68000/v1/ADD.b.json.bin -- --list
#
# Environment:
#   CONTAINER        run in this docker container instead of on the host
#   CONT_REPO        checkout path inside the container, required with CONTAINER
#   CONTAINER_USER   docker exec user (default: the container's own)
#   CONTAINER_HOME   HOME inside the container (default: read from it)
#   ARM_SYSROOT      hard-float sysroot for the ARMv7 build
#                    (default: $HOME/opt/linux-cross/arm-linux-gnueabihf)
#   QEMU, QEMU_CPU   qemu binary and -cpu for the ARMv7 build
#   NATIVE_CC, ARM_CC, ARM9_CC   passed on to the Makefile

set -eu

CONTAINER=${CONTAINER:-}
CONT_REPO=${CONT_REPO:-}
CONTAINER_USER=${CONTAINER_USER:-}
CONTAINER_HOME=${CONTAINER_HOME:-}
ARM_SYSROOT=${ARM_SYSROOT:-}
QEMU=${QEMU:-qemu-arm}
QEMU_CPU=${QEMU_CPU:-cortex-a7}

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

CORE=all
TESTS="tests/m68000/v1"
DO_BUILD=1
BUILD_ONLY=0

usage() {
    sed -n '2,/^$/s/^# \{0,1\}//p' "$0"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --core=*)      CORE="${1#--core=}"; shift ;;
        --tests=*)     TESTS="${1#--tests=}"; shift ;;
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

case "$CORE" in
    all|fame|musashi|cyclone|arm9) ;;
    *) echo "invalid --core: $CORE" >&2; exit 2 ;;
esac

if [ -n "$CONTAINER" ]; then
    if [ -z "$CONT_REPO" ]; then
        echo "error: set CONT_REPO to the checkout path inside $CONTAINER" >&2
        exit 1
    fi
    if ! docker inspect "$CONTAINER" >/dev/null 2>&1; then
        echo "error: container '$CONTAINER' not found" >&2
        exit 1
    fi
    if [ "$(docker inspect -f '{{.State.Running}}' "$CONTAINER")" != "true" ]; then
        echo "starting $CONTAINER..."
        docker start "$CONTAINER" >/dev/null
    fi
    if [ -z "$CONTAINER_HOME" ]; then
        if [ -n "$CONTAINER_USER" ]; then
            CONTAINER_HOME=$(docker exec -u "$CONTAINER_USER" "$CONTAINER" \
                sh -c 'printf %s "$HOME"')
        else
            CONTAINER_HOME=$(docker exec "$CONTAINER" sh -c 'printf %s "$HOME"')
        fi
    fi
    WORKDIR="$CONT_REPO/tools/m68k-test"
else
    CONTAINER_HOME=${CONTAINER_HOME:-$HOME}
    WORKDIR=""
fi

ARM_SYSROOT=${ARM_SYSROOT:-$CONTAINER_HOME/opt/linux-cross/arm-linux-gnueabihf}

# Run a command in the suite directory, inside the container or on the host.
dexec() {
    if [ -n "$CONTAINER" ]; then
        if [ -n "$CONTAINER_USER" ]; then
            docker exec -u "$CONTAINER_USER" -e "HOME=$CONTAINER_HOME" \
                -w "$WORKDIR" "$CONTAINER" "$@"
        else
            docker exec -e "HOME=$CONTAINER_HOME" \
                -w "$WORKDIR" "$CONTAINER" "$@"
        fi
    else
        ( cd "$SCRIPT_DIR" && "$@" )
    fi
}

if [ "$DO_BUILD" = 1 ]; then
    case "$CORE" in
        all)     MAKE_TARGET=all ;;
        fame)    MAKE_TARGET=build/m68k-test-fame ;;
        musashi) MAKE_TARGET=build/m68k-test-musashi ;;
        cyclone) MAKE_TARGET=build/m68k-test-cyclone ;;
        arm9)    MAKE_TARGET=build/m68k-test-arm9 ;;
    esac
    echo "== building $MAKE_TARGET =="
    dexec make ARM_SYSROOT="$ARM_SYSROOT" "$MAKE_TARGET"
fi

if [ "$BUILD_ONLY" = 1 ]; then
    exit 0
fi

rc=0

run_native() {
    name=$1
    shift
    echo "== $name =="
    if ! dexec "./build/m68k-test-$name" "$TESTS" "$@"; then
        rc=1
    fi
}

run_cyclone() {
    echo "== Cyclone (qemu-arm -cpu $QEMU_CPU) =="
    if ! dexec env QEMU_LD_PREFIX="$ARM_SYSROOT" \
            "$QEMU" -cpu "$QEMU_CPU" \
            ./build/m68k-test-cyclone "$TESTS" "$@"; then
        rc=1
    fi
}

run_arm9() {
    echo "== Cyclone (ARM9, qemu-arm -cpu arm946) =="
    if ! dexec "$QEMU" -cpu arm946 ./build/m68k-test-arm9 "$TESTS" "$@"; then
        rc=1
    fi
}

case "$CORE" in
    fame)    run_native fame "$@" ;;
    musashi) run_native musashi "$@" ;;
    cyclone) run_cyclone "$@" ;;
    arm9)    run_arm9 "$@" ;;
    all)
        run_native fame "$@"
        run_native musashi "$@"
        run_cyclone "$@"
        run_arm9 "$@"
        ;;
esac

exit $rc
