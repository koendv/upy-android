# android.rt confidence test -- confirms the LiteRtEnvironment/
# LiteRtCompiledModel pipeline (model load, environment/compiled-model
# creation, set_input_ndarray/invoke/get_output_ndarray) actually works
# on THIS device and THIS build, not just that the code compiles.
# Run this after installing the app on a new device, or after any
# android.rt/LiteRT change, to get a fast pass/fail instead of
# hand-checking a REPL session.
#
# Setup: copy single_add_default_a8w8_recipe_quantized.tflite
# (examples/quant/ in the repo) onto the device alongside this script,
# e.g.
#   adb push examples/quant/single_add_default_a8w8_recipe_quantized.tflite /data/local/tmp/single_add_quant.tflite
#   adb shell run-as eu.kdvelectronics.upyandroid sh -c \
#       'cat /data/local/tmp/single_add_quant.tflite > files/single_add_quant.tflite'
# (or copy both files in via the app's own Files screen). Then run this
# script the same way -- REPL paste, or Files screen "Run".
#
# single_add_default_a8w8_recipe_quantized.tflite is LiteRT's own test
# fixture (github.com/google-ai-edge/LiteRT, litert/test/testdata/,
# Apache 2.0 -- see NOTICE.md): a single int8-quantized add op, 2 inputs,
# 1 output, shape (1, 32, 32), real per-tensor scale/zero_point. Not
# android.rt-specific -- the same file already verified android.tf's
# quantize round/clamp fix earlier -- picked here because its real
# on-device output for these exact inputs is already measured and known,
# not just hand-computed.
#
# EXPECTED values below are real numbers measured on a Tab A7 through
# android.tf (the classic API) earlier this session, not theoretical.
# android.rt may dispatch through a different accelerator, so TOLERANCE
# is generous enough to absorb small cross-backend rounding differences,
# not just floating-point noise -- this is a regression/sanity check,
# not a bit-exact assertion.

from ulab import numpy as np

import android

MODEL_PATH = "/single_add_quant.tflite"
N = 32 * 32
TOLERANCE = 0.02


def check(label, in1, in2, expected):
    model = android.rt.Model(MODEL_PATH)
    a = np.full(N, in1, dtype=np.float)
    b = np.full(N, in2, dtype=np.float)
    model.set_input_ndarray(a, index=0)
    model.set_input_ndarray(b, index=1)
    model.invoke()
    out = model.get_output_ndarray()
    model.close()
    actual = float(out[0][0][0])
    ok = abs(actual - expected) <= TOLERANCE
    print(("PASS" if ok else "FAIL"), label,
          "in1=%r in2=%r expected=%.4f actual=%.4f" % (in1, in2, expected, actual))
    return ok


def main():
    print("android.rt selftest")
    print("accelerators (before any Model()):", android.rt.info())

    try:
        model = android.rt.Model(MODEL_PATH)
    except OSError as e:
        print("FAIL: could not load %s (%s)" % (MODEL_PATH, e))
        print("      copy it onto the device first -- see this script's own header comment")
        return
    print("model info:", model.info())
    model.close()

    print("accelerators (after first Model()):", android.rt.info())

    results = [
        check("sanity", 0.3, 0.0, 0.29954508),
        check("saturate", 1.25, 0.0, 0.99848368),
        check("round", 0.7002365218400001, 0.0, 0.69893852),
    ]

    if all(results):
        print("PASS")
    else:
        print("FAIL")


main()
