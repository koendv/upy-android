# test: required
# runs: host
# needs: --openmv
#
# OpenMV's own unit tests, adapted in tests/openmv/ (see README.md there),
# run on the phone. Data comes from the pinned OpenMV checkout. Each test
# runs in a fresh interpreter and has to return True.
import base64
import os
import re
import subprocess
import sys

import _upy_adb_core as core

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OPENMV = os.path.join(REPO, "upy-android", "upstream", "openmv")
DATA = os.path.join(OPENMV, "scripts", "unittest", "data")
TESTS = os.path.join(HERE, "openmv")
DEVICE_DATA = "/openmv_data"
DEVICE_TEMP = "/openmv_temp"
CHUNK = 28000  # raw bytes per adb call; a script is limited to about 48 KB
TEST_TIMEOUT = 90  # seconds

if not os.path.isdir(DATA):
    subprocess.run([os.path.join(REPO, "upy-android", "native-bringup", "fetch-upstream.sh")], check=True)


def run(src, timeout=None, reset=True):
    extras = ["reset:b:true"] if reset else []
    if timeout:
        extras.append("timeout:i:%d" % timeout)
    output, error = core.content_call("run", base64.b64encode(src.encode("utf-8")).decode("ascii"), tuple(extras))
    return output, error, core.last_status, core.last_exception


def stage_data():
    """Copies the data files the device does not have yet, by size."""
    script = (
        "import os\n"
        "for d in (%r, %r):\n"
        "    try:\n"
        "        os.mkdir(d)\n"
        "    except OSError:\n"
        "        pass\n"
        "print(sorted((n, os.stat(%r + '/' + n)[6]) for n in os.listdir(%r)))\n" % (DEVICE_DATA, DEVICE_TEMP, DEVICE_DATA, DEVICE_DATA)
    )
    output, error, _, _ = run(script)
    if error or not output or not output.startswith("["):
        sys.exit("cannot prepare %s on the phone: %r %r" % (DEVICE_DATA, output, error))
    have = dict(eval(output.strip()))
    for name in sorted(os.listdir(DATA)):
        path = os.path.join(DATA, name)
        if have.get(name) == os.path.getsize(path):
            continue
        with open(path, "rb") as f:
            data = f.read()
        for i in range(0, len(data), CHUNK):
            part = base64.b64encode(data[i:i + CHUNK]).decode("ascii")
            script = ("import binascii\nf = open(%r, %r)\nf.write(binascii.a2b_base64(%r))\nf.close()\nprint('ok')\n"
                      % (DEVICE_DATA + "/" + name, "wb" if i == 0 else "ab", part))
            output, error, _, _ = run(script, reset=False)
            if error or not output or not output.startswith("ok"):
                sys.exit("staging %s failed: %r %r" % (name, output, error))


def run_test(name):
    with open(os.path.join(TESTS, name), encoding="utf-8") as f:
        body = f.read()
    script = body + "\n\nprint('RESULT', repr(unittest(%r, %r)))\n" % (DEVICE_DATA, DEVICE_TEMP)
    output, error, status, exception = run(script, timeout=TEST_TIMEOUT)
    if error:
        return False, error
    if status == "timeout":
        return False, "timed out after %d s" % TEST_TIMEOUT
    found = re.search(r"RESULT (.*)\s*$", output or "")
    if status == "ok" and found:
        return found.group(1).strip() == "True", "returned " + found.group(1).strip()
    last = (output or "").strip().splitlines()[-1:] or [""]
    return False, "%s: %s" % (exception, last[0])


stage_data()
names = sorted(n for n in os.listdir(TESTS) if n.endswith(".py"))
failed = []
for n in names:
    ok, detail = run_test(n)
    if not ok:
        failed.append(n)
        print("FAILED: %s: %s" % (n, detail))
print("%d passed, %d failed" % (len(names) - len(failed), len(failed)))
sys.exit(1 if failed else 0)
