# Shared adb-exec plumbing for tools/upy-adb and tools/run-tests.py.
# Talks to the app's AdbExecProvider via `adb exec-out content call`.
# exec-out, not `adb shell`, avoids PTY/CRLF translation on the reply.
import base64
import os
import re
import subprocess
import time

PACKAGE = "eu.kdvelectronics.upyandroid"
URI = "content://%s.exec" % PACKAGE
ADB_TIMEOUT = 25  # seconds

# A run reply is output, then short fields (status, exception, reset) in
# any order: a Bundle prints its keys in hash order, and "output" has the
# lowest hash.
RUN_FIELD = re.compile(r", (status|exception|reset)=([A-Za-z_][A-Za-z0-9_]*)$")

# Status of the last run ("ok", "exception", "timeout"; "" if the reply had
# none), the type name of its uncaught exception ("" if none), and whether
# the interpreter was reset first ("done" or "").
last_status = ""
last_exception = ""
last_reset = ""


def adb_cmd():
    cmd = ["adb"]
    serial = os.environ.get("ANDROID_SERIAL")
    if serial:
        cmd += ["-s", serial]
    return cmd


def content_call(method, arg=None, extras=()):
    """Returns (output, error): exactly one is not None on a parsed
    reply. Both None never happens. An unparseable or timed-out reply
    is reported as a non-None error string instead."""
    cmd = adb_cmd() + ["exec-out", "content", "call", "--uri", URI, "--method", method]
    if arg is not None:
        cmd += ["--arg", arg]
    for extra in extras:
        cmd += ["--extra", extra]
    try:
        proc = subprocess.run(cmd, capture_output=True, timeout=ADB_TIMEOUT)
    except subprocess.TimeoutExpired:
        try:
            subprocess.run(
                adb_cmd() + ["exec-out", "content", "call", "--uri", URI, "--method", "interrupt"],
                capture_output=True,
                timeout=ADB_TIMEOUT,
            )
        except subprocess.TimeoutExpired:
            pass
        return None, "timed out waiting for adb/device (sent interrupt as a precaution)"

    text = proc.stdout.decode("utf-8", errors="replace")
    marker = "Result: Bundle[{"
    idx = text.rfind(marker)
    if idx == -1:
        stderr = proc.stderr.decode("utf-8", errors="replace").strip()
        return None, "unparseable adb reply: %r (stderr: %s)" % (text, stderr)

    body = text[idx + len(marker):]
    if body.endswith("}]\n"):
        body = body[: -len("}]\n")]
    elif body.endswith("}]"):
        body = body[: -len("}]")]
    else:
        return None, "unparseable adb reply: %r" % text

    if body == "":
        return "", None  # empty Bundle: reset/interrupt ack
    global last_status, last_exception, last_reset
    if body.startswith("output="):
        output = body[len("output="):]
        fields = {}
        while True:
            m = RUN_FIELD.search(output)
            if not m or m.group(1) in fields:
                break
            fields[m.group(1)] = m.group(2)
            output = output[: m.start()]
        last_status = fields.get("status", "")
        last_exception = fields.get("exception", "")
        last_reset = fields.get("reset", "")
        return output, None
    if body.startswith("error="):
        return None, body[len("error="):]
    return None, "unparseable adb reply body: %r" % body


def run_script(src_text, reset_first=True):
    """Reset (optional) + run a script's source text on-device, in one
    call. Returns (output, error) same shape as content_call."""
    b64 = base64.b64encode(src_text.encode("utf-8")).decode("ascii")
    output, error = content_call("run", b64, ("reset:b:true",) if reset_first else ())
    if error is None and reset_first and last_reset != "done":
        return None, "app too old for reset in run (adb exec protocol 3): update the app"
    return output, error


def send_input(data):
    """Bytes (or str, sent as UTF-8) for the running script's sys.stdin or
    input(). The caller adds its own newline: "yes\\n". Same shape as
    content_call: (output, error); error is "not_running" when no script
    runs."""
    if isinstance(data, str):
        data = data.encode("utf-8")
    return content_call("input", base64.b64encode(data).decode("ascii"))


def wait_for_input(timeout=30):
    """Polls status until the running script waits for input. Returns
    (prompt, None), or (None, error) after timeout seconds. An adb call
    takes about a second, so this polls about once a second."""
    deadline = time.time() + timeout
    while True:
        status, error = content_call("status")
        if error is not None:
            return None, error
        lines = status.split("\n")
        if "input: waiting" in lines:
            for line in lines:
                if line.startswith("prompt: "):
                    return line[len("prompt: "):], None
            return "", None
        if time.time() > deadline:
            return None, "timed out waiting for the script to ask for input"
        time.sleep(0.2)


def stage_fixture(local_path, device_filename):
    """Writes a local file onto the device's own VFS root (the app's
    private files dir) from inside the engine, which has full VFS
    access. Not via `adb push` + `run-as ... cat`: that failed on a test
    device with an SELinux denial. Returns (ok, error)."""
    with open(local_path, "rb") as f:
        data = f.read()
    b64 = base64.b64encode(data).decode("ascii")
    script = (
        "import binascii\n"
        "with open(%r, 'wb') as f:\n"
        "    f.write(binascii.a2b_base64(%r))\n"
        "print('staged', %r)\n" % (device_filename, b64, device_filename)
    )
    output, error = run_script(script)
    if error is not None:
        return False, error
    if not output or not output.startswith("staged "):
        return False, "unexpected output while staging %s: %r" % (device_filename, output)
    return True, None
