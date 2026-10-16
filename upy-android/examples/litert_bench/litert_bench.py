# litert_bench.py
#
# MobileNet v2 models in / on the phone: float32 and int8, 14 MB and 4 MB.
# This script offers to download the ones that are missing, from GitHub, and
# asks first. Or copy them yourself, e.g. over SFTP:
#     float32: https://raw.githubusercontent.com/google-ai-edge/LiteRT/v2.3.0/litert/test/testdata/mobilenet_v2_1.0_224.tflite
#     int8:    https://media.githubusercontent.com/media/Arm-Examples/ML-zoo/fec0bb5bcd486eb2839cdb7d437d466067402a6a/models/image_classification/mobilenet_v2_1.0_224/tflite_int8/mobilenet_v2_1.0_224_INT8.tflite
# Then, in Files, tap litert_bench.py -> Run
#
# Times MobileNet v2 run() on the CPU, GPU and NPU. An accelerator that
# does not load falls back to the CPU without an error: then its time is
# about the CPU time. An NPU usually needs the int8 model. A GPU can be
# slower than the CPU on the int8 model. The CPU is timed twice, before
# and after the GPU and NPU, because CPU speed varies on a phone (for
# example 57 and 112 ms for the same model). The NPU is compared with the
# faster of the two CPU runs.

import array
import binascii
import hashlib
import os
import time

import http
import litert

# False: download the missing models without asking.
ASK = True

MODELS = (
    ("float32", "/mobilenet_v2_1.0_224.tflite", "f"),
    ("int8", "/mobilenet_v2_1.0_224_INT8.tflite", "b"),
)
# path: (URL pinned to a tag or commit, size, SHA-256)
DOWNLOADS = {
    "/mobilenet_v2_1.0_224.tflite": (
        "https://raw.githubusercontent.com/google-ai-edge/LiteRT/v2.3.0/litert/test/testdata/mobilenet_v2_1.0_224.tflite",
        "14 MB",
        "9f3bc29e38e90842a852bfed957dbf5e36f2d97a91dd17736b1e5c0aca8d3303",
    ),
    "/mobilenet_v2_1.0_224_INT8.tflite": (
        "https://media.githubusercontent.com/media/Arm-Examples/ML-zoo/fec0bb5bcd486eb2839cdb7d437d466067402a6a/models/image_classification/mobilenet_v2_1.0_224/tflite_int8/mobilenet_v2_1.0_224_INT8.tflite",
        "4 MB",
        "bd8d56b80557a5fca7acf14f61e5339285c02810f7d7afa714b9763fc4b86e95",
    ),
}
ACCELERATORS = (
    ("CPU", litert.Accelerator.CPU),
    ("GPU", litert.Accelerator.GPU),
    ("NPU", litert.Accelerator.NPU),
    ("CPU again", litert.Accelerator.CPU),
)
RUNS = 20


def exists(path):
    try:
        os.stat(path)
        return True
    except OSError:
        return False


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            block = f.read(65536)
            if not block:
                break
            h.update(block)
    return binascii.hexlify(h.digest()).decode()


# Downloads the missing models, after asking when ASK is set. A download whose
# SHA-256 differs is deleted.
def download_missing():
    missing = [path for _, path, _ in MODELS if not exists(path)]
    if not missing:
        return
    for path in missing:
        print("missing: %s (%s), %s" % (path, DOWNLOADS[path][1], DOWNLOADS[path][0]))
    if ASK and input("Download from GitHub? [y/N] ").strip().lower() not in ("y", "yes"):
        return
    for path in missing:
        url, _, want = DOWNLOADS[path]
        try:
            http.download(url, path, progress=True)
        except OSError as e:
            print("%s: download failed, %s" % (path, e))
            continue
        if sha256(path) != want:
            os.remove(path)
            print("%s: wrong SHA-256, deleted" % path)


def ms_per_run(env, path, typecode, accelerator):
    model = litert.CompiledModel(env, path, litert.Options(accelerator))
    try:
        size = 1
        for d in model.input_shape(0):
            size *= d
        x = array.array(typecode, (0 for _ in range(size)))
        model.run(x)  # warm-up
        t = time.ticks_us()
        for _ in range(RUNS):
            model.run(x)
        return time.ticks_diff(time.ticks_us(), t) / RUNS / 1000
    finally:
        model.close()


print("litert", litert.version())
download_missing()
env = litert.Environment()
for label, path, typecode in MODELS:
    if not exists(path):
        print("%s: %s not found, skipped" % (label, path))
        continue
    times = {}
    for name, accelerator in ACCELERATORS:
        try:
            times[name] = ms_per_run(env, path, typecode, accelerator)
            print("%s %s: %.1f ms" % (label, name, times[name]))
        except OSError as e:
            print("%s %s: failed, %s" % (label, name, e))
    cpu = [times[n] for n in ("CPU", "CPU again") if n in times]
    if "NPU" in times and cpu and times["NPU"] > 0.8 * min(cpu):
        print("%s NPU: not faster than the faster CPU run, probably runs on the CPU" % label)
env.close()
