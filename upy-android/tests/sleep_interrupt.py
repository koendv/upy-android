# test: required
# runs: host
#
# Interrupt during time.sleep(10) and time.sleep_ms(10000) stops the script
# and the next script runs. An adb call takes over a second, so the latency
# is counted from the return of the interrupt call: the script must have
# ended by then, give or take 0.3 s.
import base64
import sys
import threading
import time

import _upy_adb_core as core

failures = []


def check(name, ok, detail=""):
    print("%-22s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else detail))
    if not ok:
        failures.append(name)


def run(script):
    b64 = base64.b64encode(script.encode("utf-8")).decode("ascii")
    output, error = core.content_call("run", b64)
    return output, error, core.last_status, core.last_exception


core.content_call("reset")
for label, script in (("sleep", "import time\ntime.sleep(10)\n"), ("sleep_ms", "import time\ntime.sleep_ms(10000)\n")):
    result = {}

    def background():
        result["reply"] = run(script)
        result["end"] = time.time()

    thread = threading.Thread(target=background)
    thread.start()
    time.sleep(1.5)
    core.content_call("interrupt")
    t_interrupt = time.time()
    thread.join(30)
    latency = result.get("end", t_interrupt + 99) - t_interrupt
    reply = result.get("reply", (None, None, "", ""))
    check("interrupt " + label, reply[3] == "KeyboardInterrupt" and latency < 0.3,
          "%r after %.2f s" % (reply[3], latency))
    out, err, st, exc = run("print('after')")
    check("run after " + label, (out, st) == ("after\n", "ok"), repr((out, st)))

sys.exit(1 if failures else 0)
