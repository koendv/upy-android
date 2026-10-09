# test: required
# runs: host
#
# adb exec protocol 2, checked from the PC: status, run status and
# exception, timeout, interrupt, output limit, NUL and UTF-8.
# Loops stop by themselves after 10 s, so a broken timeout or interrupt
# fails the check instead of hanging it.
import base64
import sys
import threading
import time

import _upy_adb_core as core

LOOP_10S = "import time\nt0 = time.ticks_ms()\nwhile time.ticks_diff(time.ticks_ms(), t0) < 10000:\n    time.sleep_ms(10)\n"
failures = []


def check(name, ok, detail=""):
    print("%-22s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else detail))
    if not ok:
        failures.append(name)


def run(script, extras=()):
    b64 = base64.b64encode(script.encode("utf-8")).decode("ascii")
    output, error = core.content_call("run", b64, extras)
    return output, error, core.last_status, core.last_exception


core.content_call("reset")

status, error = core.content_call("status")
fields = ("protocol: 2", "app:", "commit:", "built:", "engine: connected", "busy:", "screen:", "foreground:", "camera:")
missing = [f for f in fields if error is not None or f not in status]
check("status fields", not missing, "missing %s" % missing)

out, err, st, exc = run("print('hi')")
check("run ok", (out, err, st, exc) == ("hi\n", None, "ok", ""), repr((out, err, st, exc)))

out, err, st, exc = run("1/0")
check("run exception", (st, exc) == ("exception", "ZeroDivisionError"), repr((st, exc)))

t0 = time.time()
out, err, st, exc = run(LOOP_10S, extras=("timeout:i:2",))
elapsed = time.time() - t0
check("run timeout", st == "timeout" and elapsed < 8, "%r after %.1f s" % (st, elapsed))

result = {}


def background():
    result["reply"] = run(LOOP_10S)
    result["elapsed"] = time.time() - t0


t0 = time.time()
thread = threading.Thread(target=background)
thread.start()
time.sleep(1.5)
core.content_call("interrupt")
thread.join(30)
reply = result.get("reply", (None, None, "", ""))
check("interrupt", reply[3] == "KeyboardInterrupt" and result.get("elapsed", 99) < 8,
      "%r after %.1f s" % (reply[3], result.get("elapsed", 99)))
out, err, st, exc = run("print('after')")
check("run after interrupt", (out, st) == ("after\n", "ok"), repr((out, st)))

out, err, st, exc = run("print('a\\x00b')")
check("NUL in output", out == "a\x00b\n", repr(out))

out, err, st, exc = run("print('é中\U0001F600', len('é中\U0001F600'))")
check("UTF-8", out == "é中\U0001F600 3\n", repr(out))

out, err, st, exc = run("for n in range(1600): print('%06d ' % n + 'x' * 56)")
check("100 KB output", len(out) == 1600 * 64 and "truncated" not in out, "%d chars" % len(out or ""))

out, err, st, exc = run("for n in range(3200): print('%06d ' % n + 'x' * 56)")
check("200 KB truncated", out.endswith("[truncated]") and len(out) <= 128 * 1024, "%d chars, ends %r" % (len(out or ""), (out or "")[-20:]))

sys.exit(1 if failures else 0)
