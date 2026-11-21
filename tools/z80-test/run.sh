#!/bin/sh
# Build and run the z80 single-step harness.
#
# Usage:
#   ./run.sh [options] [-- test-options...]
#
# Options:
#   --core=all|cz80|drz80   core(s) to run (default: all)
#   --tests=PATH            test file or directory (default: tests/z80/v1 rcheck)
#   --no-build              skip the build step
#   --build-only            build and exit
#   -h, --help              show this help
#
# Examples:
#   ./run.sh
#   ./run.sh --core=cz80 -- --cycles
#   ./run.sh --tests=tests/z80/v1 -- --filter="ed 5f"
#
# Environment:
#   CONTAINER        run in this docker container instead of on the host
#   CONT_REPO        checkout path inside the container, required with CONTAINER
#   CONTAINER_USER   docker exec user (default: the container's own)
#   CONTAINER_HOME   HOME inside the container (default: read from it)
#   NATIVE_CC, ARM_CC, ARM_RUN   passed on to the Makefile

set -eu

CONTAINER=${CONTAINER:-}
CONT_REPO=${CONT_REPO:-}
CONTAINER_USER=${CONTAINER_USER:-}
CONTAINER_HOME=${CONTAINER_HOME:-}
ARM_RUN=${ARM_RUN:-qemu-arm}

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

CORE=all
TESTS="tests/z80/v1 rcheck"
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
    all|cz80|drz80) ;;
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
    WORKDIR="$CONT_REPO/tools/z80-test"
else
    CONTAINER_HOME=${CONTAINER_HOME:-$HOME}
    WORKDIR=""
fi

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
        cz80)   MAKE_TARGETS=all ;;
        drz80)  MAKE_TARGETS=drz80 ;;
        all)    MAKE_TARGETS="all drz80" ;;
    esac
    echo "== building $MAKE_TARGETS =="
    dexec make $MAKE_TARGETS
fi

if [ "$BUILD_ONLY" = 1 ]; then
    exit 0
fi

rc=0

run_cz80() {
    echo "== CZ80 =="
    if ! dexec ./build/z80-test-cz80 $TESTS "$@"; then
        rc=1
    fi
}

run_drz80() {
    echo "== DrZ80 ($ARM_RUN) =="
    if ! dexec "$ARM_RUN" ./build/z80-test-drz80 $TESTS "$@"; then
        rc=1
    fi
}

case "$CORE" in
    cz80)   run_cz80 "$@" ;;
    drz80)  run_drz80 "$@" ;;
    all)    run_cz80 "$@"; run_drz80 "$@" ;;
esac

exit $rc
