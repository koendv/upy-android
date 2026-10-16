# test: required
# runs: host
#
# litert and tflite with real models: MobileNet v2 float32 and int8 on a photo.
# The phone downloads the models and the photo with http.download (pinned URLs,
# SHA-256 checked, about 19 MB) and deletes them at the end. Needs internet on
# the phone. The test passes without an NPU or GPU: an accelerator that does
# not load falls back to the CPU without an error, so a run on it still has to
# give the same classes.
#
# Models (Apache 2.0): LiteRT's mobilenet_v2_1.0_224.tflite at tag v2.3.0, and
# Arm ML-zoo's mobilenet_v2_1.0_224_INT8.tflite at commit fec0bb5. Photo: TensorFlow's
# grace_hopper.bmp (label_image test data, Apache 2.0) at tag v2.16.1.
import re
import sys
import time

import _upy_adb_core as core

failures = []

FILES = (
    ("mn/f32.tflite", "https://raw.githubusercontent.com/google-ai-edge/LiteRT/v2.3.0/litert/test/testdata/mobilenet_v2_1.0_224.tflite",
     "9f3bc29e38e90842a852bfed957dbf5e36f2d97a91dd17736b1e5c0aca8d3303"),
    ("mn/i8.tflite", "https://media.githubusercontent.com/media/Arm-Examples/ML-zoo/fec0bb5bcd486eb2839cdb7d437d466067402a6a/models/image_classification/mobilenet_v2_1.0_224/tflite_int8/mobilenet_v2_1.0_224_INT8.tflite",
     "bd8d56b80557a5fca7acf14f61e5339285c02810f7d7afa714b9763fc4b86e95"),
    ("mn/photo.bmp", "https://raw.githubusercontent.com/tensorflow/tensorflow/v2.16.1/tensorflow/lite/examples/label_image/testdata/grace_hopper.bmp",
     "8c1165a143b3ac5c37fba13a918101133d549d3419c7fc474ae70a2f29263b80"),
)
# Classes (1001-class numbering, 653 is "military uniform"): the top class, and the
# top three as a set. The ranks after them change from one accelerator to the next.
TOP1 = 653
TOP3 = {653, 835, 907}


def check(name, ok, detail=""):
    print("%-32s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else detail))
    if not ok:
        failures.append(name)


def device(script):
    out, err = core.run_script(script)
    return (out or "") if err is None else "ERROR %s" % err


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
    "rm('mn')\n"
)

DOWNLOAD = (
    "import binascii, hashlib, os, http\n"
    "try:\n"
    "    os.mkdir('mn')\n"
    "except OSError:\n"
    "    pass\n"
    "def sha(path):\n"
    "    h = hashlib.sha256()\n"
    "    with open(path, 'rb') as f:\n"
    "        while True:\n"
    "            b = f.read(65536)\n"
    "            if not b:\n"
    "                break\n"
    "            h.update(b)\n"
    "    return binascii.hexlify(h.digest()).decode()\n"
    "for path, url, want in %r:\n"
    "    try:\n"
    "        http.download(url, path, progress=True, timeout=60)\n"
    "    except OSError as e:\n"
    "        print('DOWNLOAD', path, 'failed', e)\n"
    "        continue\n"
    "    print('DOWNLOAD', path, 'sha ok' if sha(path) == want else 'sha WRONG')\n" % (FILES,)
)

# The photo as 224x224 RGB, squashed like TensorFlow's label_image does; float32 input
# in [-1, 1], int8 input pixel - 128 (the int8 model's scale is 1/128).
RUN = r'''
import array, time, image, litert, tflite
img = image.Image('mn/photo.bmp')
if img.format() != image.RGB565:
    img = img.to_rgb565(copy=True)
r = img.copy(x_scale=224.5 / img.width(), y_scale=224.5 / img.height())
b = r.bytearray()
w = r.width()
x32 = array.array('f', [0.0] * (224 * 224 * 3))
x8 = array.array('b', bytes(224 * 224 * 3))
k = 0
for y in range(224):
    for x in range(224):
        i = (y * w + x) * 2
        v = (b[i + 1] << 8) | b[i]
        for c in ((v >> 11) << 3 | (v >> 13), ((v >> 5) & 63) << 2 | ((v >> 9) & 3), (v & 31) << 3 | ((v >> 2) & 7)):
            x32[k] = c / 127.5 - 1.0
            x8[k] = c - 128
            k += 1

def top(o, n=5):
    return sorted(range(len(o)), key=lambda i: o[i], reverse=True)[:n]

print('VERSION', litert.version())
env = litert.Environment()
for label, path, x in (('f32', 'mn/f32.tflite', x32), ('i8', 'mn/i8.tflite', x8)):
    for name in ('CPU', 'GPU', 'NPU'):
        try:
            m = litert.CompiledModel(env, path, litert.Options(getattr(litert.Accelerator, name)))
            o = m.run(x)
            m.run(x)
            t = time.ticks_us()
            for _ in range(5):
                m.run(x)
            ms = time.ticks_diff(time.ticks_us(), t) / 5000
            print('RUN', label, name, top(o), o[top(o)[0]], '%.1f' % ms)
            m.close()
        except OSError as e:
            print('RUN', label, name, 'unavailable', e)
    m = tflite.Model(path)
    print('RUN', label, 'tflite', top(m.run(x)), 0, 0)
    m.close()
env.close()
'''

ERRORS = r'''
import array, errno, litert, tflite

def err(f):
    try:
        f()
        return 'no error'
    except OSError as e:
        return 'OSError %d' % e.errno
    except ValueError:
        return 'ValueError'

env = litert.Environment()
opts = litert.Options(litert.Accelerator.CPU)
print('E litert missing model', err(lambda: litert.CompiledModel(env, 'mn/missing.tflite', opts)))
m = litert.CompiledModel(env, 'mn/f32.tflite', opts)
print('E litert wrong dtype', err(lambda: m.run(array.array('d', [0.0] * (224 * 224 * 3)))))
print('E litert too large', err(lambda: m.run(array.array('f', [0.0] * (2 * 224 * 224 * 3)))))
print('E litert too small', err(lambda: m.run(array.array('f', [0.0] * 10))))
print('E litert empty', err(lambda: m.run(array.array('f', []))))
print('E litert run after errors', len(m.run(array.array('f', [0.0] * (224 * 224 * 3)))))
print('E litert too many inputs', err(lambda: m.run(array.array('f', [0.0]), array.array('f', [0.0]))))
print('E litert shape', m.input_shape(0), m.output_shape(0))
m.close()
print('E litert closed model', err(lambda: m.run(array.array('f', [0.0] * (224 * 224 * 3)))))
env.close()
t = tflite.Model('mn/f32.tflite')
print('E tflite missing model', err(lambda: tflite.Model('mn/missing.tflite')))
print('E tflite wrong size', err(lambda: t.run(array.array('f', [0.0] * 10))))
print('E tflite wrong dtype', err(lambda: t.run(array.array('b', bytes(224 * 224 * 3)))))
print('E tflite shape', t.input_shape(0), t.output_shape(0))
t.close()
print('E tflite closed model', err(lambda: t.run(array.array('f', [0.0] * (224 * 224 * 3)))))
'''


def clean():
    for _ in range(3):
        if not device(CLEAN).startswith("ERROR"):
            return
        time.sleep(3)


clean()
try:
    out = device(DOWNLOAD)
    lines = [l for l in out.split("\n") if l.startswith("DOWNLOAD")]
    if any("failed" in l and ("UnknownHost" in l or "Unable to resolve" in l or "ETIMEDOUT" in l or "timeout" in l or "110" in l) for l in lines):
        print("\n".join(lines))
        print("the phone cannot reach the download servers (is Wi-Fi on?)")
        sys.exit(77)  # skipped, see tools/run-tests.py
    check("download and SHA-256", len(lines) == 3 and all(l.endswith("sha ok") for l in lines), out)
    if failures:
        sys.exit(1)

    out = device(RUN)
    check("run script", "VERSION" in out and "Traceback" not in out, out)
    check("litert.version() format", re.search(r"^VERSION \d+\.\d+\.\d+,\S+,\S+$", out, re.M) is not None, out)
    results = {}
    for m in re.finditer(r"^RUN (\w+) (\w+) (\[[^\]]*\]) (\S+) (\S+)$", out, re.M):
        results[(m.group(1), m.group(2))] = (eval(m.group(3)), float(m.group(4)), float(m.group(5)))
    for m in re.finditer(r"^RUN (\w+) (\w+) unavailable (.*)$", out, re.M):
        print("  %s %s unavailable: %s" % (m.group(1), m.group(2), m.group(3)))
    for model in ("f32", "i8"):
        for accelerator in ("CPU", "GPU", "NPU", "tflite"):
            if (model, accelerator) not in results:
                if accelerator in ("CPU", "tflite"):
                    check("%s %s" % (model, accelerator), False, "no result")
                continue
            ranks, score, ms = results[(model, accelerator)]
            check("%s %s classes" % (model, accelerator), ranks[0] == TOP1 and set(ranks[:3]) == TOP3, repr(ranks))
            print("  %s %-6s top %s  %s ms" % (model, accelerator, ranks[:3], "%.1f" % ms if ms else "-"))
    if ("f32", "CPU") in results:
        check("f32 CPU confidence", results[("f32", "CPU")][1] > 0.5, repr(results[("f32", "CPU")]))

    out = device(ERRORS)
    got = dict(re.findall(r"^E (.*?) (no error|OSError \d+|ValueError)$", out, re.M))
    check("error: litert missing model", got.get("litert missing model", "").startswith("OSError"), out)
    check("error: litert too many inputs", got.get("litert too many inputs") == "ValueError", out)
    check("error: litert wrong dtype", got.get("litert wrong dtype") == "OSError 22", out)
    check("error: litert too large", got.get("litert too large") == "ValueError", out)
    check("error: litert too small", got.get("litert too small") == "ValueError", out)
    check("error: litert empty", got.get("litert empty") == "ValueError", out)
    check("litert run after errors", "E litert run after errors 1001" in out, out)
    # The buffers of a failed run() once crashed the engine at the next reset.
    check("engine alive after errors", device("print('alive')") == "alive\n")
    check("error: litert closed model", got.get("litert closed model") == "OSError 22", out)
    check("error: tflite missing model", got.get("tflite missing model") == "OSError 2", out)
    check("error: tflite wrong size", got.get("tflite wrong size") == "ValueError", out)
    check("error: tflite wrong dtype", got.get("tflite wrong dtype") == "ValueError", out)
    check("error: tflite closed model", got.get("tflite closed model") == "OSError 22", out)
    check("tensor shapes", "E litert shape (1, 224, 224, 3) (1, 1001)" in out and "E tflite shape (1, 224, 224, 3) (1, 1001)" in out, out)
finally:
    clean()

sys.exit(1 if failures else 0)
