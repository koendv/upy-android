# android.litert confidence test (v0: 'kotlin' backend only -- the 'c'
# backend is designed but not yet enabled, see litert_module.cpp's own
# header comment for why. This script does not exercise set_backend('c')
# at all in v0).
#
# Same fixture and same 3 (in1, in2, expected) cases as rt_selftest.py
# (examples/quant/single_add_default_a8w8_recipe_quantized.tflite,
# TOLERANCE=0.02) -- but android.litert's surface is raw int8
# (write_int8/read_int8), matching litert-api's own Kotlin interface
# literally: there is no ndarray auto-quantize/dequantize convenience
# layer here the way android.rt's set_input_ndarray/get_output_ndarray
# has. So this script does its own quantize/dequantize arithmetic,
# using the fixture's REAL per-tensor scale/zero_point, extracted
# directly from the .tflite flatbuffer (not guessed, not copied from
# rt_selftest.py, which never needed to state them since android.rt's
# ndarray layer hides them).
#
# Setup: same as rt_selftest.py -- copy
# single_add_default_a8w8_recipe_quantized.tflite onto the device as
# /single_add_quant.tflite alongside this script, e.g.
#   adb push examples/quant/single_add_default_a8w8_recipe_quantized.tflite /data/local/tmp/single_add_quant.tflite
#   adb shell run-as eu.kdvelectronics.upyandroid cp /data/local/tmp/single_add_quant.tflite files/single_add_quant.tflite

import android

MODEL_PATH = "/single_add_quant.tflite"
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


def run_case(label, in1, in2, expected):
    env = android.litert.Environment()
    options = android.litert.Options(android.litert.Accelerator.CPU)
    model = android.litert.CompiledModel(env, MODEL_PATH, options)
    inputs = model.create_input_buffers()
    outputs = model.create_output_buffers()

    q1 = quantize(in1, SCALE_IN1, ZP_IN1)
    q2 = quantize(in2, SCALE_IN2, ZP_IN2)
    inputs[0].write_int8(bytes((q1 & 0xFF,)) * N)
    inputs[1].write_int8(bytes((q2 & 0xFF,)) * N)

    model.run(inputs, outputs)
    raw_out = outputs[0].read_int8()

    for buf in inputs + outputs:
        buf.close()
    model.close()
    env.close()

    raw0 = raw_out[0]
    signed0 = raw0 - 256 if raw0 > 127 else raw0
    actual = dequantize(signed0, SCALE_OUT, ZP_OUT)
    ok = abs(actual - expected) <= TOLERANCE
    print(("PASS" if ok else "FAIL"), label,
          "in1=%r in2=%r expected=%.4f actual=%.4f" % (in1, in2, expected, actual))
    return ok


def main():
    print("android.litert selftest (v0, 'kotlin' backend)")
    print("backend:", android.litert.get_backend())
    cases = [
        ("sanity", 0.3, 0.0, 0.29954508),
        ("saturate", 1.25, 0.0, 0.99848368),
        ("round", 0.7002365218400001, 0.0, 0.69893852),
    ]

    results = [run_case(label, in1, in2, expected) for label, in1, in2, expected in cases]
    print("PASS" if all(results) else "FAIL")


main()
