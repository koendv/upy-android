# test: required
# runs: host
#
# http: get, post, download against a server on the PC, reached over adb
# reverse (127.0.0.1) and over the PC's LAN address (plain http to a device on
# the local network). Also timeouts, redirects, errors, large bodies, Interrupt
# during connect, read, write and download, and an https smoke test.
import base64
import socket
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import _upy_adb_core as core

failures = []
stop = threading.Event()


def check(name, ok, detail=""):
    print("%-28s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else detail))
    if not ok:
        failures.append(name)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def reply(self, code, body=b"", headers=()):
        self.send_response(code)
        self.send_header("Content-Length", str(len(body)))
        for k, v in headers:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path
        if path == "/hello":
            self.reply(200, b"hello")
        elif path == "/redir-https":
            self.reply(302, headers=[("Location", "https://example.com/")])
        elif path == "/404":
            self.reply(404, b"nothing here")
        elif path == "/redir":
            self.reply(302, headers=[("Location", "/hello")])
        elif path.startswith("/size/"):
            n = int(path[len("/size/"):])
            self.send_response(200)
            self.send_header("Content-Length", str(n))
            self.end_headers()
            block = b"x" * 65536
            while n > 0:
                self.wfile.write(block[:n])
                n -= min(n, len(block))
        elif path == "/short":  # promises 1,000,000 bytes, sends 100,000, closes
            self.send_response(200)
            self.send_header("Content-Length", "1000000")
            self.end_headers()
            self.wfile.write(b"y" * 100000)
            self.close_connection = True
        elif path == "/slow":  # about 1 KB a second, for a minute
            self.send_response(200)
            self.send_header("Content-Length", "60000")
            self.end_headers()
            try:
                for _ in range(60):
                    if stop.wait(1):
                        return
                    self.wfile.write(b"z" * 1000)
                    self.wfile.flush()
            except OSError:  # the phone gave up, as an Interrupt makes it
                pass
        else:  # /stall: the request line is read, no answer
            stop.wait(60)

    def do_POST(self):
        if self.path == "/echo":
            n = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(n)
            self.reply(200, self.headers.get("Content-Type", "").encode() + b"\n" + body)
        else:  # /stall: the body is not read
            stop.wait(60)


class Server(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 64


server = Server(("0.0.0.0", 0), Handler)
port = server.server_address[1]
threading.Thread(target=server.serve_forever, daemon=True).start()

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
    s.connect(("192.0.2.1", 9))
    lan_ip = s.getsockname()[0]

LOCAL = "http://127.0.0.1:%d" % port
LAN = "http://%s:%d" % (lan_ip, port)
CLEAN = (
    "import os\n"
    "for n in ('dl.bin', 'dl.bin.part', 'dl2.bin', 'dl2.bin.part', 'dl3.bin', 'dl3.bin.part', 'dl4.bin', 'dl4.bin.part'):\n"
    "    try:\n"
    "        os.remove(n)\n"
    "    except OSError:\n"
    "        pass\n"
)


def device(script, extras=()):
    out, err = core.run_script(script)
    return (out or "") if err is None else "ERROR %s" % err


def start(script):
    result = {}

    def background():
        b64 = base64.b64encode(script.encode("utf-8")).decode("ascii")
        out, err = core.content_call("run", b64)
        result["reply"] = (out, err, core.last_status, core.last_exception)
        result["end"] = time.time()

    thread = threading.Thread(target=background)
    thread.start()
    return thread, result


import subprocess
subprocess.run(core.adb_cmd() + ["reverse", "tcp:%d" % port, "tcp:%d" % port], check=True, capture_output=True)
device(CLEAN)

try:
    out = device(
        "import android, http\n"
        "print(http.get('%s/hello'))\n"
        "print(http.get('%s/hello', timeout=5.5))\n"
        "print(http.get(url='%s/hello', timeout=0))\n"
        "print(http.post(url='%s/echo', data='k', timeout=3))\n" % (LOCAL, LOCAL, LOCAL, LOCAL)
    )
    check("get, keyword arguments", out == "b'hello'\nb'hello'\nb'hello'\nb'text/plain; charset=utf-8\\nk'\n", repr(out))

    out = device("import http\nprint(http.get('%s/hello'))\n" % LAN)
    check("get over LAN (cleartext)", out == "b'hello'\n", repr(out))

    out = device(
        "import http\n"
        "print(http.post('%s/echo', 'h\\u00e9llo'))\n"
        "print(http.post('%s/echo', b'\\x00\\x01bin', timeout=10))\n" % (LOCAL, LOCAL)
    )
    check("post str, bytes", out == "b'text/plain; charset=utf-8\\nh\\xc3\\xa9llo'\nb'application/octet-stream\\n\\x00\\x01bin'\n", repr(out))

    out = device(
        "import http\n"
        "print(http.get('%s/redir'))\n" % LOCAL
    )
    check("redirect", out == "b'hello'\n", repr(out))

    out = device(
        "import http\n"
        "def err(f, *a):\n"
        "    try:\n"
        "        f(*a)\n"
        "        return 'no error'\n"
        "    except OSError as e:\n"
        "        return str(e.errno) + ' ' + str(e.args[1])\n"
        "print(err(http.get, '%s/404'))\n"
        "print(err(http.download, '%s/404', 'dl.bin'))\n"
        "import os\n"
        "print(sorted(n for n in os.listdir('/') if n.startswith('dl')))\n" % (LOCAL, LOCAL)
    )
    check("status 404", out == "5 HTTP 404\n5 HTTP 404\n[]\n", repr(out))

    out = device(
        "import http, time\n"
        "t = time.ticks_ms()\n"
        "try:\n"
        "    http.get('%s/stall', timeout=2)\n"
        "except OSError as e:\n"
        "    d = time.ticks_diff(time.ticks_ms(), t)\n"
        "    print(e.errno, 1800 < d < 4000)\n" % LOCAL
    )
    check("read timeout", out == "110 True\n", repr(out))

    out = device(
        "import http, time\n"
        "data = bytes(8 * 1024 * 1024)\n"
        "t = time.ticks_ms()\n"
        "try:\n"
        "    http.post('%s/stall', data, timeout=2)\n"
        "    print('no error')\n"
        "except OSError as e:\n"
        "    d = time.ticks_diff(time.ticks_ms(), t)\n"
        "    print(e.errno, d < 6000)\n" % LOCAL
    )
    check("write timeout", out == "110 True\n", repr(out))

    out = device(
        "import http, time\n"
        "t = time.ticks_ms()\n"
        "try:\n"
        "    http.get('http://10.255.255.1/', timeout=2)\n"
        "except OSError as e:\n"
        "    d = time.ticks_diff(time.ticks_ms(), t)\n"
        "    print(e.errno in (110, 5, 101, 113), d < 6000)\n"
    )
    check("connect timeout", out == "True True\n", repr(out))

    out = device(
        "import android, http\n"
        "mb = android.settings()['heap_size_mb'] + 8\n"
        "try:\n"
        "    http.get('%s/size/%%d' %% (mb * 1024 * 1024))\n"
        "    print('no error')\n"
        "except MemoryError as e:\n"
        "    print('MemoryError', 'download' in str(e))\n" % LOCAL
    )
    check("get larger than memory", out == "MemoryError True\n", repr(out))

    out = device(
        "import android, http, os\n"
        "mb = android.settings()['heap_size_mb'] + 8\n"
        "http.download('%s/size/%%d' %% (mb * 1024 * 1024), 'dl.bin')\n"
        "print(os.stat('dl.bin')[6] == mb * 1024 * 1024)\n"
        "print(sorted(n for n in os.listdir('/') if n.startswith('dl')))\n" % LOCAL
    )
    check("download larger than memory", out == "True\n['dl.bin']\n", repr(out))

    out = device(
        "import http, os\n"
        "http.download('%s/size/3000000', 'dl2.bin', progress=True)\n"
        "print(os.stat('dl2.bin')[6])\n" % LOCAL
    )
    lines = out.split("\n")
    check("download progress", lines[-2] == "3000000" and lines[-3].startswith("100% (3000000 of 3000000 bytes)")
          and sum(1 for l in lines if l.startswith("100%")) == 1, repr(out))

    out = device(
        "import http, os\n"
        "try:\n"
        "    http.download('%s/short', 'dl3.bin')\n"
        "    print('no error')\n"
        "except OSError as e:\n"
        "    print('OSError')\n"
        "print(sorted(n for n in os.listdir('/') if n.startswith('dl3')))\n" % LOCAL
    )
    check("failed download leaves nothing", out == "OSError\n[]\n", repr(out))

    # Interrupt: during a read, a connect, a write and a download.
    for label, body, limit in (
        ("read", "http.get('%s/stall', timeout=30)" % LOCAL, 1.0),
        ("connect", "http.get('http://10.255.255.1/', timeout=30)", 1.0),
        ("write", "http.post('%s/stall', bytes(8 * 1024 * 1024), timeout=30)" % LOCAL, 1.0),
        ("download", "http.download('%s/slow', 'dl4.bin', timeout=30)" % LOCAL, 1.0),
    ):
        thread, result = start("import http\n" + body + "\n")
        time.sleep(3)
        core.content_call("interrupt")
        t_interrupt = time.time()
        thread.join(40)
        reply = result.get("reply", (None, None, "", ""))
        latency = result.get("end", t_interrupt + 99) - t_interrupt
        check("interrupt " + label, reply[3] == "KeyboardInterrupt" and latency < limit,
              "%r after %.2f s" % (reply[3], latency))
    out = device("import os\nprint(sorted(n for n in os.listdir('/') if n.startswith('dl4')))\n")
    check("interrupted download cleaned", out == "[]\n", repr(out))

    out = device("import http\nprint(http.help()[:5])\n")
    check("help", out == "http:\n" or out.startswith("http:"), repr(out))

    # https smoke test: needs the internet on the phone.
    try:
        socket.create_connection(("example.com", 443), timeout=5).close()
        online = True
    except OSError:
        online = False
    if online:
        out = device("import http\nb = http.get('https://example.com/')\nprint(b'Example Domain' in b)\n")
        check("https", out == "True\n", repr(out))
        out = device("import http\nb = http.get('%s/redir-https')\nprint(b'Example Domain' in b)\n" % LOCAL)
        check("redirect http to https", out == "True\n", repr(out))
    else:
        print("https                        skipped (no internet on the PC)")
finally:
    stop.set()
    server.shutdown()
    subprocess.run(core.adb_cmd() + ["reverse", "--remove", "tcp:%d" % port], capture_output=True)
    device(CLEAN)

sys.exit(1 if failures else 0)
