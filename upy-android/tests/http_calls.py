# test: required
# runs: host
#
# http: get, post, download against a server on the PC, reached over adb
# reverse (127.0.0.1) and over the PC's LAN address (plain http to a device on
# the local network). Also timeouts, redirects, errors, large bodies, Interrupt
# during connect, read, write and download, and an https smoke test.
import base64
import hashlib
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


def basic(user, password):
    return "Basic " + base64.b64encode(("%s:%s" % (user, password)).encode("utf-8")).decode("ascii")


AUTH_BASIC = basic("user", "p\u00e4ssword")  # a non-ASCII password: sent as UTF-8
AUTH_DAV = basic("user", "secret")
store = {}  # what a WebDAV PUT stored


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

    def unauthorized(self):
        self.reply(401, b"login", headers=[("WWW-Authenticate", 'Basic realm="test"')])

    def do_PUT(self):
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n)
        if self.path.startswith("/dav/"):
            if self.headers.get("Authorization") != AUTH_DAV:
                return self.unauthorized()
            if self.headers.get("Content-Type") != "image/jpeg":
                return self.reply(415, b"not a jpeg")
            store[self.path] = body
            self.reply(201)
        elif self.path == "/redir307":
            self.reply(307, headers=[("Location", "/echo-method")])
        elif self.path == "/echo-method":
            self.reply(200, ("%s %d" % (self.command, len(body))).encode())
        else:
            stop.wait(60)

    def do_GET(self):
        path = self.path
        if path == "/auth":
            header = self.headers.get("Authorization")
            if header == "Bearer tok123":
                self.reply(200, b"bearer ok")
            elif header == AUTH_BASIC:
                self.reply(200, b"basic ok")
            else:
                self.unauthorized()
        elif path == "/headers":
            self.reply(200, self.headers.get("X-Custom", "none").encode())
        elif path.startswith("/dav/"):
            self.reply(200, store[path]) if path in store else self.reply(404)
        elif path == "/seen-auth":
            self.reply(200, ("auth:" + self.headers.get("Authorization", "none")).encode())
        elif path == "/redir-xhost":
            self.reply(302, headers=[("Location", "http://%s:%d/seen-auth" % (lan_ip, port))])
        elif path == "/redir-same":
            self.reply(302, headers=[("Location", "/seen-auth")])
        elif path == "/method":
            self.reply(200, self.command.encode())
        elif path == "/hello":
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
        if self.path == "/redir302":
            self.rfile.read(int(self.headers.get("Content-Length", 0)))
            self.reply(302, headers=[("Location", "/method")])
        elif self.path == "/echo":
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

    # Authentication and headers.
    jpeg = b"\xff\xd8\xff\xe0" + bytes(range(256)) * 800
    want_sha = hashlib.sha256(jpeg).hexdigest()
    out = device(
        "import hashlib, http\n"
        "def err(f):\n"
        "    try:\n"
        "        f()\n"
        "        return 'no error'\n"
        "    except OSError as e:\n"
        "        return str(e.args[1])\n"
        "    except (ValueError, TypeError) as e:\n"
        "        return type(e).__name__\n"
        "U = %r\n"
        "print(http.get(U + '/auth', headers={'Authorization': 'Bearer tok123'}))\n"
        "print(http.get(U + '/auth', auth=('user', 'p\\u00e4ssword')))\n"
        "print(http.get(U + '/auth', auth=('u', 'p'), headers={'Authorization': 'Bearer tok123'}))\n"
        "print(err(lambda: http.get(U + '/auth')))\n"
        "print(err(lambda: http.get(U + '/auth', auth=('user', 'wrong'))))\n"
        "print(http.get(U + '/headers', headers={'X-Custom': 'a b'}))\n"
        "print(http.post(U + '/echo', 'x', headers={'content-type': 'text/csv'}))\n"
        "print(http.get(U + '/hello', 5, headers=None, auth=None))\n"
        "print(http.download(U + '/auth', 'dl.bin', headers={'Authorization': 'Bearer tok123'}), open('dl.bin', 'rb').read())\n"
        % LOCAL
    )
    check("headers and auth", out == "b'bearer ok'\nb'basic ok'\nb'bearer ok'\nHTTP 401\nHTTP 401\nb'a b'\n"
          "b'text/csv\\nx'\nb'hello'\nNone b'bearer ok'\n", repr(out))

    # A jpeg to WebDAV: PUT with Basic authentication and a content type.
    script = (
        "import hashlib, http\n"
        "U = %r\n"
        "jpeg = b'\\xff\\xd8\\xff\\xe0' + bytes(range(256)) * 800\n"
        "r = http.put(U + '/dav/pic.jpg', jpeg, headers={'Content-Type': 'image/jpeg'}, auth=('user', 'secret'))\n"
        "print(r)\n"
        "back = http.get(U + '/dav/pic.jpg')\n"
        "print(len(back), hashlib.sha256(back).digest().hex() if hasattr(back, 'hex') else '')\n"
        % LOCAL
    )
    out = device(script)
    got_sha = hashlib.sha256(store.get("/dav/pic.jpg", b"")).hexdigest()
    check("put a jpeg to WebDAV", out.startswith("b''\n") and got_sha == want_sha and len(store["/dav/pic.jpg"]) == len(jpeg), repr(out) + got_sha)
    out = device(
        "import http\n"
        "def err(f):\n"
        "    try:\n"
        "        f()\n"
        "        return 'no error'\n"
        "    except OSError as e:\n"
        "        return str(e.args[1])\n"
        "U = %r\n"
        "print(err(lambda: http.put(U + '/dav/x.jpg', b'x', auth=('user', 'secret'))))\n"
        "print(err(lambda: http.put(U + '/dav/x.jpg', b'x', headers={'Content-Type': 'image/jpeg'}, auth=('user', 'bad'))))\n"
        % LOCAL
    )
    check("put: type and login errors", out == "HTTP 415\nHTTP 401\n", repr(out))

    # Redirects: Authorization stays on the same host, goes away on another; a 302 turns
    # a post into a get and drops the data; a 307 keeps the method and the data.
    out = device(
        "import http\n"
        "U = %r\n"
        "print(http.get(U + '/redir-same', auth=('user', 'secret'))[:11])\n"
        "print(http.get(U + '/redir-xhost', auth=('user', 'secret')))\n"
        "print(http.post(U + '/redir302', 'data'))\n"
        "print(http.put(U + '/redir307', 'body'))\n"
        % LOCAL
    )
    check("redirects and credentials", out == "b'auth:Basic '\nb'auth:none'\nb'GET'\nb'PUT 4'\n", repr(out))

    # Bad arguments.
    out = device(
        "import http\n"
        "def err(f):\n"
        "    try:\n"
        "        f()\n"
        "        return 'no error'\n"
        "    except OSError as e:\n"
        "        return 'OSError'\n"
        "    except (ValueError, TypeError) as e:\n"
        "        return type(e).__name__\n"
        "U = %r\n"
        "print(err(lambda: http.get(U + '/hello', headers={'X\\n': 'v'})))\n"
        "print(err(lambda: http.get(U + '/hello', headers={'X': 'v\\r\\nY: z'})))\n"
        "print(err(lambda: http.get(U + '/hello', headers={'X': '\\u00e9'})))\n"
        "print(err(lambda: http.get(U + '/hello', headers={'': 'v'})))\n"
        "print(err(lambda: http.get(U + '/hello', headers={'a b': 'v'})))\n"
        "print(err(lambda: http.get(U + '/hello', headers=5)))\n"
        "print(err(lambda: http.get(U + '/hello', headers={1: 'v'})))\n"
        "print(err(lambda: http.get(U + '/hello', auth=('user',))))\n"
        "print(err(lambda: http.get(U + '/hello', auth='user:pw')))\n"
        "print(err(lambda: http.get('not a url')))\n"
        "print(err(lambda: http.post(U + '/echo', 'x', headers={'Content-Type': 'no/ct/at all'})))\n"
        % LOCAL
    )
    check("bad arguments", out == "ValueError\nValueError\nValueError\nValueError\nValueError\nTypeError\nTypeError\n"
          "TypeError\nTypeError\nValueError\nValueError\n", repr(out))

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
