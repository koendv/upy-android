# Shared adb-exec plumbing for tools/upy-adb and tools/run-selftests.py.
# Talks to Part 1's AdbExecProvider via `adb exec-out content call` --
# exec-out (not `adb shell`) avoids PTY/CRLF translation on the reply.
# See tools/upy-adb's own header comment for the full design story and
# the Result: Bundle[{...}] parsing rationale.
import base64
import os
import subprocess

PACKAGE = "eu.kdvelectronics.upyandroid"
URI = "content://%s.exec" % PACKAGE
ADB_TIMEOUT = 25  # seconds


def adb_cmd():
    cmd = ["adb"]
    serial = os.environ.get("ANDROID_SERIAL")
    if serial:
        cmd += ["-s", serial]
    return cmd


def content_call(method, arg=None):
    """Returns (output, error): exactly one is not None on a parsed
    reply, both None never happens -- an unparseable/timed-out reply is
    reported as a non-None error string instead."""
    cmd = adb_cmd() + ["exec-out", "content", "call", "--uri", URI, "--method", method]
    if arg is not None:
        cmd += ["--arg", arg]
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
        return "", None  # empty Bundle -- reset/interrupt ack
    if body.startswith("output="):
        return body[len("output="):], None
    if body.startswith("error="):
        return None, body[len("error="):]
    return None, "unparseable adb reply body: %r" % body


def run_script(src_text, reset_first=True):
    """Reset (optional) + run a script's source text on-device. Returns
    (output, error) same shape as content_call."""
    if reset_first:
        _, reset_err = content_call("reset")
        if reset_err is not None:
            return None, "reset failed: %s" % reset_err
    b64 = base64.b64encode(src_text.encode("utf-8")).decode("ascii")
    return content_call("run", b64)


def stage_fixture(local_path, device_filename):
    """Writes a local file onto the device's own VFS root (the app's
    private files dir) FROM INSIDE the engine, which has full VFS
    access -- not via `adb push` + `run-as ... cat`, which SESSION_STATE
    records failing on the Tab A7 with a real SELinux runas_app denial
    on an absolute-path write. Returns (ok, error)."""
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
