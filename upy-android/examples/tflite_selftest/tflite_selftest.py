# test: required
# fixture: ../quant/single_add_default_a8w8_recipe_quantized.tflite /examples/single_add_quant.tflite
# fixture: ../add_simple/add_simple.tflite /examples/add_simple.tflite
#
# tflite confidence test (top-level `tflite`). Same fixtures and cases
# as litert_selftest.py -- see that file's own header comment for
# what's different between the two modules (dtype range, accelerator
# support). tflite.Model.run(*inputs) takes/returns plain array.array,
# same shape as litert.CompiledModel.run().
#
# Models: add_simple.tflite and single_add_quant.tflite, shipped in /examples.

import array

import tflite

QUANT_MODEL_PATH = "/examples/single_add_quant.tflite"
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
    model = tflite.Model(QUANT_MODEL_PATH)

    q1 = quantize(in1, SCALE_IN1, ZP_IN1)
    q2 = quantize(in2, SCALE_IN2, ZP_IN2)
    x1 = array.array("b", [q1] * N)
    x2 = array.array("b", [q2] * N)

    y = model.run(x1, x2)

    model.close()

    actual = dequantize(y[0], SCALE_OUT, ZP_OUT)
    ok = abs(actual - expected) <= TOLERANCE
    print(("PASS" if ok else "FAIL"), label,
          "in1=%r in2=%r expected=%.4f actual=%.4f" % (in1, in2, expected, actual))
    return ok


def run_case_float():
    model = tflite.Model("/examples/add_simple.tflite")

    print("input_shape:", model.input_shape(0))
    print("output_shape:", model.output_shape(0))

    values = (1.0, 2.0, 3.0, 4.0)
    expected = (2.0, 4.0, 6.0, 8.0)
    x = array.array("f", values)

    y = model.run(x)

    model.close()

    actual = tuple(y)
    ok = len(actual) == len(expected) and all(abs(a - b) <= 1e-5 for a, b in zip(actual, expected))
    print(("PASS" if ok else "FAIL"), "float32 run()",
          "input=%r expected=%r actual=%r" % (values, expected, actual))
    return ok


def main():
    print("tflite selftest")
    cases = [
        ("sanity", 0.3, 0.0, 0.29954508),
        ("saturate", 1.25, 0.0, 0.99848368),
        ("round", 0.7002365218400001, 0.0, 0.69893852),
    ]

    results = [run_case_int8(label, in1, in2, expected) for label, in1, in2, expected in cases]
    results.append(run_case_float())
    print("PASS" if all(results) else "FAIL")


main()
