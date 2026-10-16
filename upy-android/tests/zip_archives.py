# test: required
# runs: host
#
# zipfile: info, read, write on the phone. Archives made on the phone are
# checked on the PC, and archives made on the PC (a ".." entry, a wrong CRC,
# folder entries, a corrupt file) are read on the phone.
import base64
import binascii
import os
import sys
import tempfile
import zipfile as host_zipfile

import _upy_adb_core as core

failures = []


def check(name, ok, detail=""):
    print("%-26s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else detail))
    if not ok:
        failures.append(name)


def device(script):
    out, err = core.run_script(script)
    return (out or "") if err is None else "ERROR %s" % err


def stage(path, name):
    ok, err = core.stage_fixture(path, name)
    if not ok:
        failures.append("stage " + name)
        print("stage %s FAIL %s" % (name, err))


def pull(name):
    out = device("import binascii\nprint(binascii.b2a_base64(open(%r, 'rb').read()).decode().strip())" % name)
    return base64.b64decode(out.strip())


# Everything the test creates on the phone, removed before and after the run.
LEFTOVERS = ("proj", "out", "ev", "dd", "ab", "crc", "bigout", "p.zip", "x.zip", "one.txt", "dirs.zip",
             "evil.zip", "abs.zip", "crc.zip", "junk.zip", "big.zip", "big.bin", "m.zip")
CLEAN = (
    "import os\n"
    "def rm(p):\n"
    "    try:\n"
    "        if os.stat(p)[0] & 0x4000:\n"
    "            for n in os.listdir(p):\n"
    "                rm(p + '/' + n)\n"
    "            os.rmdir(p)\n"
    "        else:\n"
    "            os.remove(p)\n"
    "    except OSError:\n"
    "        pass\n"
    "for n in %r:\n"
    "    rm(n)\n" % (LEFTOVERS,)
)


def clean():
    device(CLEAN)


clean()
work = tempfile.mkdtemp(prefix="zip-test-")

# Round trip on the phone: nested folders, an empty file, binary and text.
out = device(
    "import os, zipfile\n"
    "os.mkdir('proj')\nos.mkdir('proj/sub')\n"
    "open('proj/a.txt', 'w').write('hello ' * 1000)\n"
    "open('proj/sub/b.bin', 'wb').write(bytes(range(256)) * 40)\n"
    "open('proj/empty', 'w').close()\n"
    "zipfile.write('p.zip', 'proj')\n"
    "print(zipfile.info('p.zip'))\n"
    "zipfile.read('p.zip', 'out')\n"
    "print(open('out/proj/a.txt').read() == 'hello ' * 1000)\n"
    "print(open('out/proj/sub/b.bin', 'rb').read() == bytes(range(256)) * 40)\n"
    "print(os.stat('out/proj/empty')[6])\n"
)
expected = "[('proj/a.txt', 6000), ('proj/empty', 0), ('proj/sub/b.bin', 10240)]\nTrue\nTrue\n0\n"
check("round trip", out == expected, repr(out))

# The archive from the phone is valid on the PC.
data = pull("p.zip")
with open(os.path.join(work, "p.zip"), "wb") as f:
    f.write(data)
z = host_zipfile.ZipFile(os.path.join(work, "p.zip"))
check("PC reads phone zip", z.testzip() is None and z.namelist() == ["proj/a.txt", "proj/empty", "proj/sub/b.bin"], repr(z.namelist()))
check("deflated", z.getinfo("proj/a.txt").compress_size < 600, repr(z.getinfo("proj/a.txt").compress_size))
z.close()

# write replaces an existing zip, a file works as path, the zip inside the folder is skipped.
out = device(
    "import os, zipfile\n"
    "open('one.txt', 'w').write('1')\n"
    "zipfile.write('x.zip', 'proj')\n"
    "zipfile.write('x.zip', 'one.txt')\n"
    "print(zipfile.info('x.zip'))\n"
    "zipfile.write('proj/self.zip', 'proj')\n"
    "print([n for n, s in zipfile.info('proj/self.zip')])\n"
)
check("replace, file, self skip", out == "[('one.txt', 1)]\n['proj/a.txt', 'proj/empty', 'proj/sub/b.bin']\n", repr(out))

# Archives made on the PC.
with host_zipfile.ZipFile(os.path.join(work, "dirs.zip"), "w") as z:
    z.writestr("d/", "")
    z.writestr("d/f.txt", "in folder")
    z.writestr("top.txt", "top")
stage(os.path.join(work, "dirs.zip"), "dirs.zip")
out = device(
    "import zipfile\n"
    "print(zipfile.info('dirs.zip'))\n"
    "zipfile.read('dirs.zip', 'dd')\n"
    "print(open('dd/d/f.txt').read(), open('dd/top.txt').read())\n"
)
check("PC zip with folder entry", out == "[('d/', 0), ('d/f.txt', 9), ('top.txt', 3)]\nin folder top\n", repr(out))

with host_zipfile.ZipFile(os.path.join(work, "evil.zip"), "w") as z:
    z.writestr("ok.txt", "fine")
    z.writestr("../evil.txt", "bad")
stage(os.path.join(work, "evil.zip"), "evil.zip")
out = device(
    "import os, zipfile\n"
    "for f in (lambda: zipfile.info('evil.zip'), lambda: zipfile.read('evil.zip', 'ev/inner')):\n"
    "    try:\n"
    "        f()\n"
    "        print('no error')\n"
    "    except OSError as e:\n"
    "        print('OSError', e.errno)\n"
    "print(sorted(os.listdir('/')).count('evil.txt'), 'ok.txt' in os.listdir('ev/inner'))\n"
)
check("zip-slip entry", out == "OSError 5\nOSError 5\n0 False\n", repr(out))

with host_zipfile.ZipFile(os.path.join(work, "abs.zip"), "w") as z:
    z.writestr("/abs.txt", "bad")
stage(os.path.join(work, "abs.zip"), "abs.zip")
out = device(
    "import os, zipfile\n"
    "try:\n"
    "    zipfile.read('abs.zip', 'ab')\n"
    "    print('no error')\n"
    "except OSError as e:\n"
    "    print('OSError')\n"
    "print('abs.txt' in os.listdir('/'))\n"
)
check("leading slash entry", out == "OSError\nFalse\n", repr(out))

# A wrong CRC: a stored entry with one changed data byte.
path = os.path.join(work, "crc.zip")
with host_zipfile.ZipFile(path, "w", host_zipfile.ZIP_STORED) as z:
    z.writestr("first.txt", "first")
    z.writestr("second.txt", "SECOND-DATA")
raw = bytearray(open(path, "rb").read())
raw[raw.index(b"SECOND-DATA")] ^= 1
open(path, "wb").write(raw)
stage(path, "crc.zip")
out = device(
    "import os, zipfile\n"
    "try:\n"
    "    zipfile.read('crc.zip', 'crc')\n"
    "    print('no error')\n"
    "except OSError as e:\n"
    "    print('OSError', 'CRC' in str(e))\n"
    "print(os.listdir('crc'))\n"
)
check("wrong CRC", out == "OSError True\n['first.txt']\n", repr(out))

# Errors.
open(os.path.join(work, "junk.zip"), "wb").write(b"this is not a zip file" * 10)
stage(os.path.join(work, "junk.zip"), "junk.zip")
out = device(
    "import zipfile\n"
    "def err(f, *a):\n"
    "    try:\n"
    "        f(*a)\n"
    "        return 'no error'\n"
    "    except OSError as e:\n"
    "        return e.errno\n"
    "print(err(zipfile.info, 'junk.zip'))\n"
    "print(err(zipfile.info, 'missing.zip'))\n"
    "print(err(zipfile.read, 'missing.zip', 'x'))\n"
    "print(err(zipfile.write, 'm.zip', 'missing'))\n"
    "print(err(zipfile.info, '../p.zip'))\n"
    "print(err(zipfile.read, 'p.zip', 'one.txt'))\n"
)
lines = out.split("\n")
import errno
check("corrupt file", lines[0] == str(errno.EIO), repr(out))
check("missing file", lines[1:4] == [str(errno.ENOENT)] * 3, repr(out))
check("path outside root", lines[4] == str(errno.EIO), repr(out))
check("dest is a file", lines[5] == str(errno.EIO), repr(out))

# An entry larger than the heap is written and extracted without holding it in memory.
out = device(
    "import android, os, zipfile\n"
    "mb = android.settings()['heap_size_mb'] + 8\n"
    "block = bytes(1024 * 1024)\n"
    "with open('big.bin', 'wb') as f:\n"
    "    for _ in range(mb):\n"
    "        f.write(block)\n"
    "zipfile.write('big.zip', 'big.bin')\n"
    "print(zipfile.info('big.zip') == [('big.bin', mb * 1024 * 1024)])\n"
    "os.remove('big.bin')\n"
    "zipfile.read('big.zip', 'bigout')\n"
    "print(os.stat('bigout/big.bin')[6] == mb * 1024 * 1024)\n"
    "os.remove('bigout/big.bin')\nos.remove('big.zip')\n"
)
check("entry larger than heap", out == "True\nTrue\n", repr(out))

clean()
print(device("import zipfile\nprint(zipfile.help())")[:60].replace("\n", " "))
sys.exit(1 if failures else 0)
