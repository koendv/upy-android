#!/bin/sh
# Runs upstream MicroPython's own tests/run-tests.py against a real
# connected device, through tools/upy-adb (Part 1's AdbExecProvider)
# instead of a host-built `unix` binary -- Part 9 of the dev-workflow-
# speedups plan, decision point 1 ("use on-device test, using adb
# exec"). Needs exactly one device connected (set ANDROID_SERIAL if more
# than one), with the app installed and Settings > adb-exec turned on.
#
# Usage:
#   tools/run-upstream-tests.sh [run-tests.py args...]
#   tools/run-upstream-tests.sh -d basics        # a real, dependency-free subset to start with
#   tools/run-upstream-tests.sh tests/basics/int1.py
#
# MICROPYTHON_TOP overrides the pinned upstream checkout to test
# against (default: this project's own read-only historical-reference
# clone -- never write into it; that's exactly why -r below points
# elsewhere instead of run-tests.py's own default).
set -e
cd "$(dirname "$0")/.."

MICROPYTHON_TOP=${MICROPYTHON_TOP:-/home/koen/src/repos/upy-android/upstream/micropython}
if [ ! -f "$MICROPYTHON_TOP/tests/run-tests.py" ]; then
    echo "run-upstream-tests: $MICROPYTHON_TOP/tests/run-tests.py not found -- set MICROPYTHON_TOP" >&2
    exit 1
fi

RESULT_DIR=$(mktemp -d)
export MICROPY_MICROPYTHON="$(pwd)/tools/upy-adb"

# -j1: ScriptExecCore (BoardManager.kt) is a single-engine, single-
# -connection singleton with its own busy gate -- run-tests.py's own
# default (-j = cpu_count(), parallel) would just serialize into a pile
# of spurious "busy" failures instead of real ones.
# -r: run-tests.py's own default (tests/results, inside MICROPYTHON_TOP)
# would write into the frozen historical-reference checkout -- never
# edit/write there, see project memory. A fresh mktemp -d instead.
if python3 "$MICROPYTHON_TOP/tests/run-tests.py" -j1 -r "$RESULT_DIR" "$@"; then
    rm -rf "$RESULT_DIR"
    exit 0
else
    status=$?
    echo "run-upstream-tests: failures -- kept results in $RESULT_DIR ($MICROPYTHON_TOP/tests/run-tests.py --print-failures -r $RESULT_DIR to see diffs)" >&2
    exit $status
fi
