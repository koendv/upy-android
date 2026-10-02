# litert confidence test (top-level `litert`).
# litert.CompiledModel.run(*inputs) takes/returns ulab.numpy ndarrays
# directly -- no TensorBuffer, no write_*/read_* methods, no
# quantization/scale/zero_point handling by the module itself. v1
# supports float32 and int8 only (the Kotlin litert-api itself has no
# uint8/int16/uint16 path). A script that wants a quantized model's
# real-world values does its own scale/zero_point math on the raw int8
# ndarray, same as this test does below.
#
# Setup: same two fixtures as before.
#   adb push examples/quant/single_add_default_a8w8_recipe_quantized.tflite /data/local/tmp/single_add_quant.tflite
#   adb shell run-as eu.kdvelectronics.upyandroid sh -c \
#       'cat /data/local/tmp/single_add_quant.tflite > files/single_add_quant.tflite'
#   adb push examples/add_simple/add_simple.tflite /data/local/tmp/add_simple.tflite
#   adb shell run-as eu.kdvelectronics.upyandroid sh -c \
#       'cat /data/local/tmp/add_simple.tflite > files/add_simple.tflite'

from ulab import numpy as np

import litert

QUANT_MODEL_PATH = "/single_add_quant.tflite"
N = 32 * 32
TOLERANCE = 0.02

SCALE_IN1, ZP_IN1 = 0.003918503411114216, -128
SCALE_IN2, ZP_IN2 = 0.003918628674000502, -128
SCALE_OUT, ZP_OUT = 0.0076806433498859406, -128


def quantize(v, scale, zp):
    raw = round(v / scale) + zp
    return max(-128, min(127, raw))


def dequantize(raw, scale, zp):
    return (raw - zp) * scale


def run_case_int8(label, in1, in2, expected):
    env = litert.Environment()
    options = litert.Options(litert.Accelerator.CPU)
    model = litert.CompiledModel(env, QUANT_MODEL_PATH, options)

    q1 = quantize(in1, SCALE_IN1, ZP_IN1)
    q2 = quantize(in2, SCALE_IN2, ZP_IN2)
    x1 = np.array([q1] * N, dtype=np.int8)
    x2 = np.array([q2] * N, dtype=np.int8)

    y = model.run(x1, x2)

    model.close()
    env.close()

    raw0 = int(y[0])
    actual = dequantize(raw0, SCALE_OUT, ZP_OUT)
    ok = abs(actual - expected) <= TOLERANCE
    print(("PASS" if ok else "FAIL"), label,
          "in1=%r in2=%r expected=%.4f actual=%.4f" % (in1, in2, expected, actual))
    return ok


def run_case_float():
    env = litert.Environment()
    options = litert.Options(litert.Accelerator.CPU)
    model = litert.CompiledModel(env, "/add_simple.tflite", options)

    values = (1.0, 2.0, 3.0, 4.0)
    expected = (2.0, 4.0, 6.0, 8.0)
    x = np.array(values, dtype=np.float)

    y = model.run(x)

    model.close()
    env.close()

    actual = tuple(y)
    ok = len(actual) == len(expected) and all(abs(a - b) <= 1e-5 for a, b in zip(actual, expected))
    print(("PASS" if ok else "FAIL"), "float32 run()",
          "input=%r expected=%r actual=%r" % (values, expected, actual))
    return ok


def main():
    print("litert selftest")
    cases = [
        ("sanity", 0.3, 0.0, 0.29954508),
        ("saturate", 1.25, 0.0, 0.99848368),
        ("round", 0.7002365218400001, 0.0, 0.69893852),
    ]

    results = [run_case_int8(label, in1, in2, expected) for label, in1, in2, expected in cases]
    results.append(run_case_float())
    print("PASS" if all(results) else "FAIL")


main()
