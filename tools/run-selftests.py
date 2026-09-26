#!/usr/bin/env python3
# Runs this project's OWN examples/*_selftest confidence tests on a
# connected device via Part 1's AdbExecProvider, comparing captured
# output against a companion .exp file next to each script (same
# convention upstream MicroPython's own run-tests.py uses, see
# tools/upy-adb/tools/run-upstream-tests.sh for that side of Part 9).
#
# These are NOT upstream MicroPython tests -- they're this project's
# own Android-module confidence tests (mediastore/litert/ml/...), so
# there's no CPython ground truth to diff against the way run-tests.py
# gets one. First run against a given script has no baseline yet: it
# records the actual captured output as the new .exp and reports
# RECORDED rather than PASS/FAIL. Only trust a RECORDED baseline once
# you've read it and confirmed it looks right -- this script has no way
# to know a first-ever run's output is correct, only that it's
# deterministic-looking (see --record to intentionally re-baseline).
#
# Default set is deterministic and self-contained: mediastore, litert,
# ml. Left OUT of the default set, opt-in only:
#   fileprovider -- pops a real OS share sheet; the API call succeeding
#     is not the same as the sheet actually appearing on screen (see
#     project memory: don't claim a hardware/UI effect happened just
#     because the API call succeeded) -- only visually verifiable.
#   mqtt -- needs a real external broker (test.mosquitto.org) AND a
#     retained "ping" message already published from the host before
#     this runs (see examples/mqtt_selftest/mqtt_selftest.py's own
#     header comment) -- this script does not publish that ping for
#     you.
#
# Usage:
#   tools/run-selftests.py                        # default set
#   tools/run-selftests.py --fileprovider --mqtt   # include the opt-in ones
#   tools/run-selftests.py --only litert           # just one, by name
#   tools/run-selftests.py --record                # force re-baseline even if .exp exists
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _upy_adb_core as core

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXAMPLES_DIR = os.path.join(REPO_ROOT, "upy-android", "examples")

# name -> (script relative to EXAMPLES_DIR, [(local fixture rel to
# EXAMPLES_DIR, device VFS filename), ...], opt_in)
TESTS = {
    "mediastore": ("mediastore_selftest/mediastore_selftest.py", [], False),
    "litert": (
        "litert_selftest/litert_selftest.py",
        [
            ("quant/single_add_default_a8w8_recipe_quantized.tflite", "/single_add_quant.tflite"),
            ("add_simple/add_simple.tflite", "/add_simple.tflite"),
        ],
        False,
    ),
    "ml": (
        "ml_selftest/ml_selftest.py",
        [("add_simple/add_simple.tflite", "/add_simple.tflite")],
        False,
    ),
    "fileprovider": ("fileprovider_selftest/fileprovider_selftest.py", [], True),
    "mqtt": ("mqtt_selftest/mqtt_selftest.py", [], True),
}


def exp_path_for(script_path):
    return script_path + ".exp"


def run_one(name, force_record):
    rel_script, fixtures, _opt_in = TESTS[name]
    script_path = os.path.join(EXAMPLES_DIR, rel_script)
    exp_path = exp_path_for(script_path)

    for local_rel, device_filename in fixtures:
        local_path = os.path.join(EXAMPLES_DIR, local_rel)
        ok, err = core.stage_fixture(local_path, device_filename)
        if not ok:
            return "ERROR", "fixture staging (%s -> %s) failed: %s" % (
                local_rel,
                device_filename,
                err,
            )

    with open(script_path, "r", encoding="utf-8") as f:
        src = f.read()

    output, error = core.run_script(src)
    if error is not None:
        return "ERROR", error

    if force_record or not os.path.exists(exp_path):
        with open(exp_path, "w", encoding="utf-8") as f:
            f.write(output)
        return "RECORDED", output

    with open(exp_path, "r", encoding="utf-8") as f:
        expected = f.read()

    if output == expected:
        return "PASS", None

    diff_lines = []
    exp_lines = expected.splitlines(keepends=True)
    got_lines = output.splitlines(keepends=True)
    import difflib

    diff_lines = list(
        difflib.unified_diff(exp_lines, got_lines, fromfile=exp_path, tofile="(actual)")
    )
    return "FAIL", "".join(diff_lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fileprovider", action="store_true", help="include the opt-in fileprovider test")
    parser.add_argument("--mqtt", action="store_true", help="include the opt-in mqtt test")
    parser.add_argument("--only", metavar="NAME", help="run just this one test by name")
    parser.add_argument("--record", action="store_true", help="force re-recording the .exp baseline")
    args = parser.parse_args()

    if args.only:
        if args.only not in TESTS:
            sys.stderr.write("unknown test %r -- choices: %s\n" % (args.only, ", ".join(TESTS)))
            sys.exit(2)
        names = [args.only]
    else:
        names = [
            name
            for name, (_, _, opt_in) in TESTS.items()
            if not opt_in or (name == "fileprovider" and args.fileprovider) or (name == "mqtt" and args.mqtt)
        ]

    results = {}
    for name in names:
        status, detail = run_one(name, args.record)
        results[name] = status
        print("%-14s %s" % (name, status))
        if status in ("FAIL", "ERROR") and detail:
            print(detail if status == "FAIL" else "  " + detail)
        elif status == "RECORDED":
            print("  (no prior baseline -- wrote %s; read it before trusting it)" % exp_path_for(
                os.path.join(EXAMPLES_DIR, TESTS[name][0])
            ))

    failed = [n for n, s in results.items() if s in ("FAIL", "ERROR")]
    print()
    print("%d/%d passed or recorded" % (len(results) - len(failed), len(results)))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
