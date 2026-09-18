# android.tf confidence test -- confirms the whole LiteRT pipeline
# (model load, NNAPI/CPU delegate, set_input/invoke/get_output) actually
# works on THIS device and THIS build, not just that the code compiles.
# Run this after installing the app on a new device, or after any
# android.tf/LiteRT change, to get a fast pass/fail instead of
# hand-checking a REPL session.
#
# Setup: copy add_simple.tflite (same directory as this script in the
# repo, examples/tf_selftest/) onto the device alongside this script,
# e.g.
#   adb push examples/tf_selftest/add_simple.tflite /data/local/tmp/add_simple.tflite
#   adb shell run-as eu.kdvelectronics.upyandroid sh -c \
#       'cat /data/local/tmp/add_simple.tflite > files/add_simple.tflite'
# (or copy both files in via the app's own Files screen). Then run this
# script the same way -- REPL paste, or Files screen "Run".
#
# add_simple.tflite is LiteRT's own test fixture
# (github.com/google-ai-edge/LiteRT, litert/test/testdata/, Apache 2.0 --
# see NOTICE.md), not a real model: one op (tfl.add) that doubles a
# 4-element float32 input, shape (2, 2). Picked because it's tiny (516
# bytes) and its correct output is exactly known, not because it's
# representative.

import struct

import android

MODEL_PATH = "/add_simple.tflite"
INPUT = (1.0, 2.0, 3.0, 4.0)
EXPECTED_OUTPUT = (2.0, 4.0, 6.0, 8.0)
TOLERANCE = 1e-5


def main():
    print("android.tf selftest")
    print("info:", android.tf.info())

    try:
        model = android.tf.Model(MODEL_PATH)
    except OSError as e:
        print("FAIL: could not load %s (%s)" % (MODEL_PATH, e))
        print("      copy it onto the device first -- see this script's own header comment")
        return

    print("model info:", model.info())

    try:
        model.set_input(struct.pack("4f", *INPUT))
        model.invoke()
        output = struct.unpack("4f", model.get_output())
    except Exception as e:
        print("FAIL:", e)
        return
    finally:
        model.close()

    if any(abs(a - b) > TOLERANCE for a, b in zip(output, EXPECTED_OUTPUT)):
        print("FAIL: output", output, "does not match expected", EXPECTED_OUTPUT)
        return

    print("output:", output)
    print("PASS")


main()
