#!/usr/bin/env python3
# Runs this project's tests on a connected phone or emulator through
# adb exec, and compares each script's output with the .exp file next to
# it. Test scripts are upy-android/examples/*_selftest/*.py and
# upy-android/tests/*.py. Markers in each script's header set its tier and
# fixtures; see upy-android/tests/README.md.
#
# Exit code is 1 only if a required test fails. A known-failure test that
# passes is reported as "unexpectedly passed", not as an error.
#
# A missing .exp is an error. --record writes the current output as the
# .exp (read it before trusting it); it refuses known-failure tests, whose
# .exp must hold the correct output, written by hand.
#
# Usage:
#   tools/run-tests.py                 # required and known-failure tests
#   tools/run-tests.py --manual        # also manual tests
#   tools/run-tests.py --only litert   # one test, by name
#   tools/run-tests.py --list          # list tests with their tier
#   tools/run-tests.py --record --only litert   # write litert's .exp
import argparse
import difflib
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _upy_adb_core as core

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST_GLOBS = [
    os.path.join(REPO_ROOT, "upy-android", "examples", "*_selftest", "*.py"),
    os.path.join(REPO_ROOT, "upy-android", "tests", "*.py"),
]
TIERS = ("required", "known-failure", "manual")


class Test:
    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)[: -len(".py")]
        if self.name.endswith("_selftest"):
            self.name = self.name[: -len("_selftest")]
        self.tier = None
        self.reason = ""
        self.fixtures = []  # (local path, device path)
        self.problems = []
        self._read_markers()

    def _read_markers(self):
        with open(self.path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line.startswith("#"):
                    break
                body = line[1:].strip()
                if body.startswith("test:"):
                    words = body[len("test:"):].split(None, 1)
                    if not words or words[0] not in TIERS:
                        self.problems.append("unknown tier in %r" % line)
                        continue
                    self.tier = words[0]
                    self.reason = words[1] if len(words) > 1 else ""
                elif body.startswith("fixture:"):
                    parts = body[len("fixture:"):].split()
                    if len(parts) != 2:
                        self.problems.append("fixture needs <local> <device>: %r" % line)
                        continue
                    local = os.path.normpath(os.path.join(os.path.dirname(self.path), parts[0]))
                    self.fixtures.append((local, parts[1]))
        if self.tier is None and not self.problems:
            self.problems.append("no '# test:' marker")


def discover():
    tests = []
    for pattern in TEST_GLOBS:
        for path in sorted(glob.glob(pattern)):
            tests.append(Test(path))
    return tests


def run_one(test, force_record):
    exp_path = test.path + ".exp"
    if force_record and test.tier == "known-failure":
        return "ERROR", "--record refuses known-failure tests: write the correct output to %s by hand" % exp_path
    if not force_record and not os.path.exists(exp_path):
        return "ERROR", "no %s: write it, or run --record --only %s" % (exp_path, test.name)
    for local, device in test.fixtures:
        ok, err = core.stage_fixture(local, device)
        if not ok:
            return "ERROR", "fixture %s -> %s: %s" % (local, device, err)

    with open(test.path, "r", encoding="utf-8") as f:
        src = f.read()
    output, error = core.run_script(src)
    if error is not None:
        return "ERROR", error

    if force_record:
        with open(exp_path, "w", encoding="utf-8") as f:
            f.write(output)
        return "RECORDED", "wrote %s; read it before trusting it" % exp_path

    with open(exp_path, "r", encoding="utf-8") as f:
        expected = f.read()
    if output == expected:
        return "PASS", None
    diff = difflib.unified_diff(
        expected.splitlines(keepends=True),
        output.splitlines(keepends=True),
        fromfile=exp_path,
        tofile="(actual)",
    )
    return "FAIL", "".join(diff)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--manual", action="store_true", help="also run manual tests")
    parser.add_argument("--only", metavar="NAME", help="run one test, by name")
    parser.add_argument("--list", action="store_true", help="list tests with their tier")
    parser.add_argument("--record", action="store_true", help="write the current output as .exp")
    args = parser.parse_args()

    tests = discover()
    seen = {}
    for t in tests:
        if t.name in seen:
            t.problems.append("same name as %s" % os.path.relpath(seen[t.name].path, REPO_ROOT))
        seen.setdefault(t.name, t)
    broken = [t for t in tests if t.problems]
    for t in broken:
        for p in t.problems:
            print("marker error: %s: %s" % (os.path.relpath(t.path, REPO_ROOT), p))
    if broken:
        sys.exit(2)

    if args.list:
        for t in tests:
            print("%-14s %-14s %s" % (t.name, t.tier, t.reason))
        return

    if args.only:
        selected = [t for t in tests if t.name == args.only]
        if not selected:
            sys.stderr.write("unknown test %r -- choices: %s\n" % (args.only, ", ".join(t.name for t in tests)))
            sys.exit(2)
    else:
        selected = [t for t in tests if t.tier != "manual" or args.manual]

    results = []
    for t in selected:
        status, detail = run_one(t, args.record)
        if t.tier == "known-failure":
            status = {"FAIL": "XFAIL", "PASS": "XPASS"}.get(status, status)
        results.append((t, status))
        label = {"XFAIL": "XFAIL (known failure)", "XPASS": "XPASS (unexpectedly passed)"}.get(status, status)
        print("%-14s %-14s %s" % (t.name, t.tier, label))
        if detail and status in ("FAIL", "ERROR", "RECORDED"):
            print(detail if status == "FAIL" else "  " + detail)

    counts = {}
    for _, status in results:
        counts[status] = counts.get(status, 0) + 1
    print()
    print(", ".join("%d %s" % (n, s) for s, n in sorted(counts.items())))
    failed = [t.name for t, s in results if t.tier == "required" and s in ("FAIL", "ERROR")]
    if failed:
        print("required tests failed: %s" % ", ".join(failed))
    if not args.only and not any(t.tier == "required" for t, _ in results):
        print("no required tests ran")
        sys.exit(1)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
