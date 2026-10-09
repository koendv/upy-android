#!/bin/sh
# Runs upstream MicroPython's own tests/run-tests.py against a real
# connected device, through tools/upy-adb (AdbExecProvider)
# instead of a host-built `unix` binary. Needs exactly one device
# connected (set ANDROID_SERIAL if more than one), with the app
# installed and Settings > adb-exec turned on.
#
# Usage:
#   tools/run-micropython-tests.sh [run-tests.py args...]
#   tools/run-micropython-tests.sh -d basics        # a real, dependency-free subset to start with
#   tools/run-micropython-tests.sh basics/int1.py   # test paths are relative to tests/
#
# MICROPYTHON_TOP overrides the pinned upstream checkout to test
# against. Default: the checkout the build fetches (upy-android/upstream/
# micropython, pinned in upstream.properties). Don't write into it; -r below
# points elsewhere for that reason.
set -e
cd "$(dirname "$0")/.."

MICROPYTHON_TOP=${MICROPYTHON_TOP:-$(pwd)/upy-android/upstream/micropython}
if [ ! -f "$MICROPYTHON_TOP/tests/run-tests.py" ]; then
    echo "run-micropython-tests: $MICROPYTHON_TOP/tests/run-tests.py not found -- set MICROPYTHON_TOP" >&2
    exit 1
fi

RESULT_DIR=$(mktemp -d)
export MICROPY_MICROPYTHON="$(pwd)/tools/upy-adb"

# -j1: ScriptExecCore (BoardManager.kt) is a single-engine, single-
# connection singleton with its own busy gate. run-tests.py's own
# default (-j = cpu_count(), parallel) would just serialize into a pile
# of spurious "busy" failures instead of real ones.
# -r: run-tests.py's own default (tests/results, inside MICROPYTHON_TOP)
# would write into the frozen historical-reference checkout. Never edit
# or write there. A fresh mktemp -d instead.
# run-tests.py runs each test with its relative directory as cwd, so it
# must itself run from tests/, like upstream (cd tests; ./run-tests.py).
cd "$MICROPYTHON_TOP/tests"
if python3 run-tests.py -j1 -r "$RESULT_DIR" "$@"; then
    rm -rf "$RESULT_DIR"
    exit 0
else
    status=$?
    echo "run-micropython-tests: failures -- kept results in $RESULT_DIR ($MICROPYTHON_TOP/tests/run-tests.py --print-failures -r $RESULT_DIR to see diffs)" >&2
    exit $status
fi
