# test: required
# runs: host
#
# The SSH shell and script input: code runs in command mode, each line goes to
# a waiting script in input mode (also one started from adb), the output of a
# running script is shown while it runs, Ctrl+C ends input. The test switches
# SSH on in the app's Settings screen with a password (uiautomator taps; SSH
# must be off and the password empty before), connects over adb forward, and
# puts the settings back at the end. The phone screen must be on.
import base64
import os
import re
import subprocess
import sys
import threading
import time

# tests/asyncio.py is a device test and would hide the real asyncio from paramiko.
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != HERE]
import paramiko  # noqa: E402

import _upy_adb_core as core  # noqa: E402

PKG = "eu.kdvelectronics.upyandroid"
PASSWORD = "ssh-input-test"
failures = []


def check(name, ok, detail=""):
    print("%-30s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else detail))
    if not ok:
        failures.append(name)


def adb(*args, data=None):
    return subprocess.run(core.adb_cmd() + list(args), input=data, capture_output=True)


def ui_nodes():
    adb("shell", "uiautomator", "dump", "/sdcard/ui.xml")
    xml = adb("shell", "cat", "/sdcard/ui.xml").stdout.decode("utf-8", errors="replace")
    nodes = []
    for m in re.finditer(r"<node[^>]*?>", xml):
        n = m.group(0)
        b = re.search(r'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', n)
        nodes.append({
            "text": (re.search(r'text="([^"]*)"', n) or re.search(r"(.*)", "")).group(1),
            "cls": (re.search(r'class="([^"]*)"', n) or re.search(r"(.*)", "")).group(1).split(".")[-1],
            "checked": 'checked="true"' in n,
            "clickable": 'clickable="true"' in n,
            "box": tuple(int(v) for v in b.groups()) if b else (0, 0, 0, 0),
        })
    return nodes


def tap_at(x, y):
    adb("shell", "input", "tap", str(x), str(y))


def label_y(nodes, label):
    """Vertical centre of the (lowest) node whose text starts with label."""
    found = [n for n in nodes if n["text"].startswith(label)]
    if not found:
        sys.exit("the Settings screen has no %r: %r" % (label, [n["text"] for n in nodes if n["text"]][:12]))
    box = max(found, key=lambda n: n["box"][1])["box"]
    return (box[1] + box[3]) // 2


def row_checked(nodes, label):
    y = label_y(nodes, label)
    return any(n["checked"] and n["clickable"] and n["box"][1] <= y <= n["box"][3] for n in nodes)


def open_settings():
    """Brings the Settings screen up; retries, as a freshly installed app starts slowly."""
    adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
    adb("shell", "input", "swipe", "540", "2000", "540", "500", "300")
    adb("shell", "am", "start", "-n", PKG + "/.MainActivity")
    for _ in range(10):
        time.sleep(2)
        nodes = ui_nodes()
        if any(n["text"].startswith("Enable SSH") for n in nodes):
            return nodes
        tabs = [n for n in nodes if n["text"] == "Settings"]
        if not tabs:
            # The tab bar is hidden while the keyboard shows: Back hides the keyboard.
            adb("shell", "input", "keyevent", "4")
        if tabs:
            box = max(tabs, key=lambda n: n["box"][1])["box"]
            tap_at((box[0] + box[2]) // 2, (box[1] + box[3]) // 2 - 40)
    sys.exit("the Settings screen did not come up")


def settings_ssh(on):
    nodes = open_settings()
    if on:
        if row_checked(nodes, "Enable SSH"):
            sys.exit("SSH is already on; the test needs it off to start")
        field_y = label_y(nodes, "SSH password")
        tap_at(540, field_y)
        time.sleep(0.5)
        adb("shell", "input", "text", PASSWORD)
        time.sleep(0.5)
        adb("shell", "input", "keyevent", "4")
        time.sleep(0.5)
        tap_at(540, label_y(ui_nodes(), "Enable SSH"))
        time.sleep(2)
        if not row_checked(ui_nodes(), "Enable SSH"):
            sys.exit("could not switch SSH on")
    else:
        if row_checked(nodes, "Enable SSH"):
            tap_at(540, label_y(nodes, "Enable SSH"))
            time.sleep(1)
        tap_at(540, label_y(ui_nodes(), "SSH password"))
        time.sleep(0.5)
        adb("shell", "input", "keyevent", "KEYCODE_MOVE_END")
        for _ in range(len(PASSWORD) + 4):
            adb("shell", "input", "keyevent", "KEYCODE_DEL")
        adb("shell", "input", "keyevent", "4")


class Shell:
    """One SSH shell: everything it printed is in text; expect() waits for a string."""

    def __init__(self, port):
        self.client = paramiko.SSHClient()
        self.client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        self.client.connect("127.0.0.1", port=port, username="u", password=PASSWORD,
                            look_for_keys=False, allow_agent=False, timeout=15)
        self.chan = self.client.invoke_shell()
        self.text = ""
        self.mark = 0
        self.times = []
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        while True:
            try:
                data = self.chan.recv(4096)
            except Exception:
                return
            if not data:
                return
            self.text += data.decode("utf-8", errors="replace")
            self.times.append((len(self.text), time.time()))

    def send(self, s):
        self.chan.send(s)

    def expect(self, needle, timeout=20):
        end = time.time() + timeout
        while time.time() < end:
            at = self.text.find(needle, self.mark)
            if at >= 0:
                self.mark = at + len(needle)
                return True
            time.sleep(0.05)
        return False

    def time_of(self, needle):
        at = self.text.find(needle)
        for length, t in self.times:
            if length > at:
                return t
        return None

    def answer(self, prompt, reply):
        """Waits for the prompt, then types the reply. The pause is for the app's
        "a script waits" message, which follows the prompt by a few milliseconds."""
        ok = self.expect(prompt)
        time.sleep(0.5)
        self.send(reply)
        return ok

    def close(self):
        self.client.close()


port = None
shells = []
try:
    settings_ssh(True)
    port = int(adb("forward", "tcp:0", "tcp:2222").stdout.decode().strip())
    deadline = time.time() + 40
    while True:
        try:
            first = Shell(port)
            break
        except Exception as e:
            if time.time() > deadline:
                sys.exit("cannot connect over SSH: %r" % e)
            time.sleep(2)
    shells.append(first)
    sh = first
    check("banner and prompt", sh.expect("upy shell") and sh.expect(">>> "), sh.text)

    # Command mode.
    sh.send("print(1 + 2)\r\r")
    check("command mode", sh.expect("3\r\n") and sh.expect(">>> "), sh.text[-200:])

    # Output while the script runs: 'a' arrives before the script ends.
    sh.send("import time\rprint('a')\rtime.sleep(3)\rprint('b')\r\r")
    got_a = sh.expect("a\r\n")
    got_b = sh.expect("b\r\n")
    ta, tb = sh.time_of("\r\na\r\n") or sh.time_of("a\r\n"), sh.time_of("b\r\n")
    check("live output", got_a and got_b and ta is not None and tb is not None and tb - ta > 2.0, "a at %r, b at %r" % (ta, tb))
    sh.expect(">>> ")

    # Input mode: a script started here asks, the answer is a line.
    sh.send("name = input('name? ')\rprint('hello', name)\r\r")
    check("prompt shown while waiting", sh.expect("name? "), sh.text[-200:])
    time.sleep(0.5)
    sh.send("alice\r")
    check("answer reaches the script", sh.expect("hello alice") and sh.expect(">>> "), sh.text[-200:])

    # An empty line is an answer.
    sh.send("x = input('go? ')\rprint(repr(x))\r\r")
    sh.answer("go? ", "\r")
    check("empty answer", sh.expect("''") and sh.expect(">>> "), sh.text[-200:])

    # "exit" is an answer, not a command, while a script waits.
    sh.send("a = input('q? ')\rprint(repr(a))\r\r")
    sh.answer("q? ", "exit\r")
    check("exit as an answer", sh.expect("'exit'") and sh.expect(">>> "), sh.text[-200:])
    sh.send("print(5)\r\r")
    check("session still open", sh.expect("5\r\n") and sh.expect(">>> "), sh.text[-200:])

    # Two prompts in one script.
    sh.send("a = input('one? ')\rb = input('two? ')\rprint(a + b)\r\r")
    sh.answer("one? ", "1\r")
    sh.answer("two? ", "2\r")
    check("two prompts", sh.expect("12") and sh.expect(">>> "), sh.text[-200:])

    # Ctrl+C ends input.
    sh.send("input('stop? ')\r\r")
    sh.answer("stop? ", "\x03")
    check("ctrl+c while waiting", sh.expect("^C") and sh.expect(">>> "), sh.text[-200:])
    status, _ = core.content_call("status")
    check("input mode ended", status is not None and "input: no" in status, status)

    # A script started from adb waits; the SSH lines go to it.
    result = {}
    script = "print('from adb')\nx = input('adb asks? ')\nprint('adb got', x)\n"
    thread = threading.Thread(target=lambda: result.update(r=core.content_call("run", base64.b64encode(script.encode()).decode())))
    thread.start()
    core.wait_for_input(30)
    check("adb script prompt shown", sh.answer("adb asks? ", "zed\r"), sh.text[-200:])
    thread.join(30)
    check("answer to an adb script", result.get("r", ("", None))[0] is not None and "adb got zed" in (result["r"][0] or ""), repr(result.get("r")))
    check("adb output shown", sh.expect("adb got zed"), sh.text[-200:])

    # A session that joins while a script already waits.
    thread = threading.Thread(target=lambda: result.update(r=core.content_call("run", base64.b64encode(script.encode()).decode())))
    thread.start()
    core.wait_for_input(30)
    late = Shell(port)
    shells.append(late)
    check("late session notice", late.expect("[a script waits for input]"), late.text[-300:])
    late.send("late\r")
    thread.join(30)
    check("late session answers", "adb got late" in (result["r"][0] or ""), repr(result.get("r")))

    sh.send("\x04")
    check("ctrl+d resets", sh.expect("(reset)"), sh.text[-200:])
finally:
    for s in shells:
        try:
            s.close()
        except Exception:
            pass
    if port is not None:
        adb("forward", "--remove", "tcp:%d" % port)
    settings_ssh(False)

sys.exit(1 if failures else 0)
