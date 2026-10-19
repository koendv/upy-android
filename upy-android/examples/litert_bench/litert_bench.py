# litert_bench.py
#
# MobileNet v2 models and a photo in /rom on the phone: float32 and int8 models,
# 14 MB and 4 MB, and a photo of 1 MB. This script downloads the ones that
# are missing, from GitHub. Or copy them yourself, e.g. over SFTP:
#     float32: https://raw.githubusercontent.com/google-ai-edge/LiteRT/v2.3.0/litert/test/testdata/mobilenet_v2_1.0_224.tflite
#     int8:    https://media.githubusercontent.com/media/Arm-Examples/ML-zoo/fec0bb5bcd486eb2839cdb7d437d466067402a6a/models/image_classification/mobilenet_v2_1.0_224/tflite_int8/mobilenet_v2_1.0_224_INT8.tflite
#     photo:   https://raw.githubusercontent.com/tensorflow/tensorflow/v2.16.1/tensorflow/lite/examples/label_image/testdata/grace_hopper.bmp
# Then, in Files, tap litert_bench.py -> Run
#
# Times MobileNet v2 run() on the CPU, GPU and NPU, and checks the result.
# The GPU is run three times: with the backend LiteRT chooses (default),
# with the OpenCL backend and with the OpenGL backend. If the default row
# differs from the OpenCL row, the default is not OpenCL on this phone.
# The photo goes through every accelerator. The top class and the top three
# classes must equal those of the first CPU run, else the line ends with
# WRONG: do not use that accelerator for that model on this phone. The
# classes are MobileNet's 1001 class numbers; the photo is class 653,
# military uniform. An accelerator that does not load falls back to the CPU
# without an error: then its time is about the CPU time. An NPU usually
# needs the int8 model. A GPU can be slower than the CPU on the int8 model.
# The CPU is timed twice, before and after the GPU and NPU, because CPU
# speed varies on a phone (for example 57 and 112 ms for the same model).
# The NPU is compared with the faster of the two CPU runs.

import array
import binascii
import hashlib
import os
import time

import http
import image
import litert

PHOTO = "/rom/grace_hopper.bmp"
MODELS = (
    ("float32", "/rom/mobilenet_v2_1.0_224.tflite", "f"),
    ("int8", "/rom/mobilenet_v2_1.0_224_INT8.tflite", "b"),
)
# path: (URL pinned to a tag or commit, size, SHA-256)
DOWNLOADS = {
    "/rom/mobilenet_v2_1.0_224.tflite": (
        "https://raw.githubusercontent.com/google-ai-edge/LiteRT/v2.3.0/litert/test/testdata/mobilenet_v2_1.0_224.tflite",
        "14 MB",
        "9f3bc29e38e90842a852bfed957dbf5e36f2d97a91dd17736b1e5c0aca8d3303",
    ),
    "/rom/mobilenet_v2_1.0_224_INT8.tflite": (
        "https://media.githubusercontent.com/media/Arm-Examples/ML-zoo/fec0bb5bcd486eb2839cdb7d437d466067402a6a/models/image_classification/mobilenet_v2_1.0_224/tflite_int8/mobilenet_v2_1.0_224_INT8.tflite",
        "4 MB",
        "bd8d56b80557a5fca7acf14f61e5339285c02810f7d7afa714b9763fc4b86e95",
    ),
    PHOTO: (
        "https://raw.githubusercontent.com/tensorflow/tensorflow/v2.16.1/tensorflow/lite/examples/label_image/testdata/grace_hopper.bmp",
        "1 MB",
        "8c1165a143b3ac5c37fba13a918101133d549d3419c7fc474ae70a2f29263b80",
    ),
}
ACCELERATORS = (
    ("CPU", litert.Options(litert.Accelerator.CPU)),
    ("GPU (default)", litert.Options(litert.Accelerator.GPU)),
    ("GPU (OpenCL)", litert.Options(litert.Accelerator.GPU, backend=litert.Backend.OPENCL)),
    ("GPU (OpenGL)", litert.Options(litert.Accelerator.GPU, backend=litert.Backend.OPENGL)),
    ("NPU", litert.Options(litert.Accelerator.NPU)),
    ("CPU again", litert.Options(litert.Accelerator.CPU)),
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


# Downloads the missing models and photo. A download whose SHA-256 differs is deleted.
def download_missing():
    missing = [path for path in [m[1] for m in MODELS] + [PHOTO] if not exists(path)]
    if not missing:
        return
    for path in missing:
        print("downloading %s (%s), %s" % (path, DOWNLOADS[path][1], DOWNLOADS[path][0]))
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


# The photo as 224x224 RGB, squashed like TensorFlow's label_image does:
# float32 input in [-1, 1], int8 input pixel - 128. None without the photo.
def photo_inputs():
    if not exists(PHOTO):
        return None
    img = image.Image(PHOTO)
    if img.format() != image.RGB565:
        img = img.to_rgb565(copy=True)
    r = img.copy(x_scale=224.5 / img.width(), y_scale=224.5 / img.height())
    b = r.bytearray()
    w = r.width()
    x32 = array.array("f", [0.0] * (224 * 224 * 3))
    x8 = array.array("b", bytes(224 * 224 * 3))
    k = 0
    for y in range(224):
        for x in range(224):
            i = (y * w + x) * 2
            v = (b[i + 1] << 8) | b[i]
            for c in ((v >> 11) << 3 | (v >> 13), ((v >> 5) & 63) << 2 | ((v >> 9) & 3), (v & 31) << 3 | ((v >> 2) & 7)):
                x32[k] = c / 127.5 - 1.0
                x8[k] = c - 128
                k += 1
    return x32, x8


def top(output, n=3):
    return sorted(range(len(output)), key=lambda i: output[i], reverse=True)[:n]


# (ms per run, top three classes of the first run)
def run_model(env, path, x, options):
    model = litert.CompiledModel(env, path, options)
    try:
        classes = top(model.run(x))  # also the warm-up
        t = time.ticks_us()
        for _ in range(RUNS):
            model.run(x)
        return time.ticks_diff(time.ticks_us(), t) / RUNS / 1000, classes
    finally:
        model.close()


print("litert", litert.version())
download_missing()
inputs = photo_inputs()
if inputs is None:
    print("%s not found: speed only, the results are not checked" % PHOTO)
env = litert.Environment()
wrong = []
not_run = []
equal = 0
for label, path, typecode in MODELS:
    if not exists(path):
        print("%s: %s not found, skipped" % (label, path))
        continue
    if inputs is not None:
        x = inputs[0] if typecode == "f" else inputs[1]
    else:
        size = 1
        model = litert.CompiledModel(env, path, litert.Options(litert.Accelerator.CPU))
        for d in model.input_shape(0):
            size *= d
        model.close()
        x = array.array(typecode, (0 for _ in range(size)))
    times = {}
    reference = None
    for name, options in ACCELERATORS:
        try:
            ms, classes = run_model(env, path, x, options)
        except OSError as e:
            not_run.append("%s %s" % (label, name))
            print("%s %s: did not run, %s" % (label, name, e))
            continue
        times[name] = ms
        if inputs is None:
            print("%s %s: %.1f ms" % (label, name, ms))
        elif reference is None:
            reference = classes
            print("%s %s: %.1f ms, reference, top class %d" % (label, name, ms, classes[0]))
        elif classes[0] == reference[0] and set(classes) == set(reference):
            equal += 1
            print("%s %s: %.1f ms, ok" % (label, name, ms))
        else:
            wrong.append("%s %s" % (label, name))
            print("%s %s: %.1f ms, WRONG: top classes %s, CPU %s" % (label, name, ms, classes, reference))
    cpu = [times[n] for n in ("CPU", "CPU again") if n in times]
    if "NPU" in times and cpu and times["NPU"] > 0.8 * min(cpu):
        print("%s NPU: not faster than the faster CPU run, probably runs on the CPU" % label)
env.close()
if inputs is not None:
    print("Checked against the CPU: %d equal, %d WRONG, %d did not run." % (equal, len(wrong), len(not_run)))
    if wrong:
        print("WRONG: %s. Do not use these on this phone." % ", ".join(wrong))
    if not_run:
        print("Did not run: %s." % ", ".join(not_run))
