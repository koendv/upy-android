# test: required
# runs: host
#
# Script input: input(), sys.stdin.readline(), read(n), read(), type-ahead,
# poll on sys.stdin, the status lines, the adb input errors, Interrupt and
# Reset while a script waits. Answers are sent with adb input; the typed
# bytes are echoed in the app's log, not in the run output.
import base64
import sys
import threading
import time

import _upy_adb_core as core

failures = []


def check(name, ok, detail=""):
    print("%-24s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else detail))
    if not ok:
        failures.append(name)


def start(script):
    """Runs script in a thread; result["reply"] is (output, error, status, exception)."""
    result = {}

    def background():
        b64 = base64.b64encode(script.encode("utf-8")).decode("ascii")
        output, error = core.content_call("run", b64)
        result["reply"] = (output, error, core.last_status, core.last_exception)

    thread = threading.Thread(target=background)
    thread.start()
    return thread, result


def answer(text):
    prompt, error = core.wait_for_input(30)
    if error is not None:
        return None, error
    out, err = core.send_input(text)
    return prompt, err


core.content_call("reset")

out, err = core.send_input("x\n")
check("not_running", (out, err) == (None, "not_running"), repr((out, err)))

thread, result = start(
    "import sys\n"
    "a = input('name? ')\n"
    "b = sys.stdin.readline()\n"
    "c = sys.stdin.read(3)\n"
    "d = sys.stdin.read()\n"
    "print(repr((a, b, c, d)))\n"
)
prompt, err = answer("alice\n")
check("input prompt", (prompt, err) == ("name? ", None), repr((prompt, err)))
prompt, err = answer("second\n")
check("readline", err is None, repr(err))
prompt, err = answer("abcde\n")
check("read(n)", err is None, repr(err))
thread.join(30)
out = result["reply"][0] or ""
# read(3) takes "abc"; read() takes the rest of the FIFO, "de\n", without waiting.
check("input/read results", out == "name? ('alice', 'second\\n', 'abc', 'de\\n')\n", repr(out))

# Type-ahead: bytes sent while the script is busy wait in the FIFO.
thread, result = start("import time\ntime.sleep(4)\nprint(repr(input()))\n")
time.sleep(1)
out, err = core.send_input("early\n")
check("type-ahead accepted", (out, err) == ("", None), repr((out, err)))
thread.join(30)
check("type-ahead read", result["reply"][0] == "'early'\n", repr(result["reply"]))

# read() does not wait when the FIFO has bytes, and returns "" ... after waiting once when empty.
thread, result = start("import sys\nprint(repr(sys.stdin.read()))\n")
prompt, err = answer("hello")
thread.join(30)
check("read() waits if empty", result["reply"][0] == "'hello'\n", repr(result["reply"]))

# poll on sys.stdin returns readable when input arrives.
thread, result = start(
    "import select, sys, time\n"
    "p = select.poll()\n"
    "p.register(sys.stdin, select.POLLIN)\n"
    "t = time.ticks_ms()\n"
    "r = p.poll(20000)\n"
    "print(len(r), time.ticks_diff(time.ticks_ms(), t) < 15000, repr(sys.stdin.readline()))\n"
)
time.sleep(2)
out, err = core.send_input("polled\n")
thread.join(40)
check("poll on stdin", result["reply"][0] == "1 True 'polled\\n'\n", repr(result["reply"]))

# Interrupt while waiting.
thread, result = start("input('q? ')\nprint('never')\n")
core.wait_for_input(30)
core.content_call("interrupt")
thread.join(30)
reply = result["reply"]
check("interrupt while waiting", reply[3] == "KeyboardInterrupt" and "never" not in (reply[0] or ""), repr(reply))
status, err = core.content_call("status")
check("input cleared", err is None and "input: no" in status, repr(status))

# Reset while waiting.
thread, result = start("input('q? ')\nprint('never')\n")
core.wait_for_input(30)
core.content_call("reset")
thread.join(30)
status, err = core.content_call("status")
check("reset while waiting", err is None and "input: no" in status and "never" not in (result["reply"][0] or ""), repr(status))

# FIFO emptied at script start: bytes left over from one script do not reach the next.
thread, result = start("import time\ntime.sleep(2)\n")
time.sleep(0.5)
core.send_input("leftover\n")
thread.join(30)
thread, result = start("import sys\nprint(repr(sys.stdin.read()))\n")
core.wait_for_input(30)
core.send_input("fresh")
thread.join(30)
check("fifo cleared at start", result["reply"][0] == "'fresh'\n", repr(result["reply"]))

# print and stderr go to the log; the answer is echoed in the log, not in the run output.
out, err = core.run_script("import sys\nprint('a')\nsys.stdout.write('b\\n')\nsys.stderr.write('c\\n')\n")
check("stdout, stderr", out == "a\nb\nc\n", repr(out))

out, err = core.content_call("input", "a")
check("bad base64", (out, err) == (None, "bad_base64"), repr((out, err)))

sys.exit(1 if failures else 0)
