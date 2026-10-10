# test: required
# runs: host
# needs: --micropython
#
# MicroPython's own basics tests, from the pinned MicroPython clone, run on
# the phone. Exceptions are in micropython_known_failures.txt. Fails on a
# failure that is not listed there; a listed test that passes is reported.
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
MICROPYTHON = os.path.join(REPO, "upy-android", "upstream", "micropython")
UPY_ADB = os.path.join(REPO, "tools", "upy-adb")

if not os.path.exists(os.path.join(MICROPYTHON, "tests", "run-tests.py")):
    subprocess.run([os.path.join(REPO, "upy-android", "native-bringup", "fetch-upstream.sh")], check=True)

known, skips = {}, []
with open(os.path.join(HERE, "micropython_known_failures.txt"), encoding="utf-8") as f:
    for line in f:
        if not line.strip() or line.startswith("#"):
            continue
        kind, test, reason = line.split(None, 2)
        if kind == "known-failure":
            known[test] = reason.strip()
        elif kind == "skip":
            skips.append(test)
        else:
            sys.exit("micropython_known_failures.txt: unknown kind %r" % kind)

result_dir = tempfile.mkdtemp(prefix="micropython-tests-")
cmd = [sys.executable, "run-tests.py", "-j1", "-r", result_dir, "-d", "basics"]
for test in skips:
    cmd += ["-e", "^" + re.escape(test) + "$"]
env = dict(os.environ, MICROPY_MICROPYTHON=UPY_ADB)
subprocess.run(cmd, cwd=os.path.join(MICROPYTHON, "tests"), env=env, stdout=subprocess.DEVNULL)

with open(os.path.join(result_dir, "_results.json"), encoding="utf-8") as f:
    results = json.load(f)["results"]
passed = [t for t, r, _ in results if r == "pass"]
failed = [t for t, r, _ in results if r == "fail"]
skipped = [t for t, r, _ in results if r == "skip"]

unexpected = [t for t in failed if t not in known]
fixed = [t for t in passed if t in known]
print("%d passed, %d failed, %d skipped" % (len(passed), len(failed), len(skipped)))
for t in failed:
    print("  %s %s" % ("known failure:" if t in known else "FAILED:", t))
for t in fixed:
    print("  unexpectedly passed: %s" % t)
if unexpected:
    print("diffs: %s --print-failures -r %s" % (os.path.join(MICROPYTHON, "tests", "run-tests.py"), result_dir))
if not passed:
    sys.exit("no tests passed")
sys.exit(1 if unexpected else 0)
