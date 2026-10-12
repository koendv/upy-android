# litert_bench.py
#
# - copy one or both models to / on the phone, e.g. over SFTP:
#     float32: https://raw.githubusercontent.com/google-ai-edge/LiteRT/main/litert/test/testdata/mobilenet_v2_1.0_224.tflite
#     int8:    https://media.githubusercontent.com/media/Arm-Examples/ML-zoo/master/models/image_classification/mobilenet_v2_1.0_224/tflite_int8/mobilenet_v2_1.0_224_INT8.tflite
# - in Files, tap litert_bench.py -> Run
#
# Times MobileNet v2 run() on the CPU, GPU and NPU. An accelerator that
# does not load falls back to the CPU without an error: then its time is
# about the CPU time. An NPU usually needs the int8 model. A GPU can be
# slower than the CPU on the int8 model. Times vary from run to run.

import array
import os
import time

import litert

MODELS = (
    ("float32", "/mobilenet_v2_1.0_224.tflite", "f"),
    ("int8", "/mobilenet_v2_1.0_224_INT8.tflite", "b"),
)
ACCELERATORS = (
    ("CPU", litert.Accelerator.CPU),
    ("GPU", litert.Accelerator.GPU),
    ("NPU", litert.Accelerator.NPU),
)
RUNS = 20


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
env = litert.Environment()
for label, path, typecode in MODELS:
    try:
        os.stat(path)
    except OSError:
        print("%s: %s not found, skipped" % (label, path))
        continue
    times = {}
    for name, accelerator in ACCELERATORS:
        try:
            times[name] = ms_per_run(env, path, typecode, accelerator)
            print("%s %s: %.1f ms" % (label, name, times[name]))
        except OSError as e:
            print("%s %s: failed, %s" % (label, name, e))
    if "NPU" in times and "CPU" in times and times["NPU"] > 0.8 * times["CPU"]:
        print("%s NPU: not faster than the CPU, probably runs on the CPU" % label)
env.close()
