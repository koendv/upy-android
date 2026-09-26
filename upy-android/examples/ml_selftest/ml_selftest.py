# ml/tf (TFLM backend) confidence test -- confirms the real interpreter +
# MicroMutableOpResolver<113> vendored in Part 5 actually runs inference
# on THIS device and THIS build, not just that the code compiles. Runs
# the SAME known-good fixture android.tf/android.rt's own selftests use
# (see examples/tf_selftest/tf_selftest.py), through the new top-level
# `ml`/`tf` module instead.
#
# Setup: same as tf_selftest.py -- copy add_simple.tflite onto the
# device's VFS root first, e.g.
#   adb push examples/tf_selftest/add_simple.tflite /data/local/tmp/add_simple.tflite
#   adb shell run-as eu.kdvelectronics.upyandroid sh -c \
#       'cat /data/local/tmp/add_simple.tflite > files/add_simple.tflite'

from ulab import numpy as np

import ml

MODEL_PATH = "/add_simple.tflite"
INPUT = (1.0, 2.0, 3.0, 4.0)
EXPECTED_OUTPUT = (2.0, 4.0, 6.0, 8.0)
TOLERANCE = 1e-5


def main():
    print("ml selftest (TFLM backend)")

    try:
        # postprocess is MP_ARG_REQUIRED in py_ml.c despite having a
        # None default -- must be passed explicitly.
        model = ml.Model(MODEL_PATH, postprocess=None)
    except OSError as e:
        print("FAIL: could not load %s (%s)" % (MODEL_PATH, e))
        print("      copy it onto the device first -- see this script's own header comment")
        return

    print("input_shape:", model.input_shape)
    print("input_dtype:", model.input_dtype)
    print("output_shape:", model.output_shape)
    print("output_dtype:", model.output_dtype)

    try:
        input_array = np.array(INPUT, dtype=np.float).reshape((2, 2))
        outputs = model.predict([input_array])
        output = tuple(float(v) for v in outputs[0].flatten())
    except Exception as e:
        print("FAIL:", e)
        return

    if any(abs(a - b) > TOLERANCE for a, b in zip(output, EXPECTED_OUTPUT)):
        print("FAIL: output", output, "does not match expected", EXPECTED_OUTPUT)
        return

    print("output:", output)
    print("PASS")


main()
