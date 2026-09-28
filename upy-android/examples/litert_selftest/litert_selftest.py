# litert confidence test (top-level `litert`, was android.litert).
# litert deliberately stays a faithful, literal mirror of litert-api's
# own Kotlin TensorBuffer surface: write_int8/read_int8 (raw bytes)
# plus write_float/read_float/write_int/read_int/write_bool/read_bool/
# write_long/read_long (typed arrays, one real Kotlin method each),
# with no ndarray/auto-quantize convenience layer of its own (tried and
# dropped). `ml`/`tf` covers that use case now. android.rt, which used
# to, has been deleted.
#
# Setup: same two fixtures ml_selftest.py also uses.
#   adb push examples/quant/single_add_default_a8w8_recipe_quantized.tflite /data/local/tmp/single_add_quant.tflite
#   adb shell run-as eu.kdvelectronics.upyandroid sh -c \
#       'cat /data/local/tmp/single_add_quant.tflite > files/single_add_quant.tflite'
#   adb push examples/add_simple/add_simple.tflite /data/local/tmp/add_simple.tflite
#   adb shell run-as eu.kdvelectronics.upyandroid sh -c \
#       'cat /data/local/tmp/add_simple.tflite > files/add_simple.tflite'

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


def run_case_float():
    # add_simple.tflite: real typed write_float()/read_float(), not
    # write_int8()/read_int8(). Exercises the new typed pair directly,
    # matching litert-api's own TensorBuffer.writeFloat()/readFloat().
    env = litert.Environment()
    options = litert.Options(litert.Accelerator.CPU)
    model = litert.CompiledModel(env, "/add_simple.tflite", options)
    inputs = model.create_input_buffers()
    outputs = model.create_output_buffers()

    values = (1.0, 2.0, 3.0, 4.0)
    expected = (2.0, 4.0, 6.0, 8.0)
    inputs[0].write_float(values)

    model.run(inputs, outputs)
    actual = outputs[0].read_float()

    for buf in inputs + outputs:
        buf.close()
    model.close()
    env.close()

    ok = len(actual) == len(expected) and all(abs(a - b) <= 1e-5 for a, b in zip(actual, expected))
    print(("PASS" if ok else "FAIL"), "write_float/read_float",
          "input=%r expected=%r actual=%r" % (values, expected, tuple(actual)))
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
