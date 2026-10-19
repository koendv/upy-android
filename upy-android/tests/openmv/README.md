# OpenMV unit tests, adapted

These are OpenMV's own unit tests (`scripts/unittest/tests` in the OpenMV repository), changed to run on this port. OpenMV's tests are MIT licensed, Copyright (c) 2013-2023 OpenMV LLC. Each file starts with a header that names the original and lists the changes.

OpenMV has 95 tests. 82 are here. 13 are left out, see [Left out](#left-out).

## Run

```
tools/run-tests.py --openmv
```

The phone must have **Settings > Enable adb exec** on. The harness is `tests/openmv_unittests.py`. It copies OpenMV's test images (2.3 MB, from the pinned OpenMV checkout in `upy-android/upstream/openmv`) to `/openmv_data` on the phone. Later runs copy only what is missing. Then each test runs in a fresh interpreter and has to return `True`. A run takes about 2 minutes, and 4 minutes the first time.

## Changes

| Files | Change |
|---|---|
| 58 | none |
| 18 | `copy_to_fb=True` removed. This port has no frame buffer, and the tests only read the image. |
| `jpeg_compression`, `png_compression` | `img.to_rgb565()` becomes `img = img.to_rgb565(copy=True)`. The in-place conversion needs the frame buffer. |
| `pixel_formats` | The conversions use `copy=True`, for the same reason. |
| `keypoints_find_match`, `keypoints_save_load` | `find_keypoints` uses `corner_detector=image.CORNER_FAST`. The default detector, AGAST, is GPL and not in this port. The expected values are measured on this port. OpenMV's values come from AGAST. |
| `haarcascade` | Uses this app's cascade in `/rom` and values measured on this port. OpenMV's test cascade finds no face in the test image here, and the cause is not known. |

The tests that compare against values measured on this port guard against changes. They do not show that the values agree with OpenMV.

## Left out

Not applicable, 13:

- `capture` needs the `csi` camera module. The app has `camera`.
- `networking` needs OpenMV's `omv`, `network`, `requests` and `mqtt` modules.
- `tensorflow` and the ten `ml_*` tests (`ml_blazeface`, `ml_face_landmarks`, `ml_fomo`, `ml_hand_landmarks`, `ml_movenet`, `ml_palm_detection`, `ml_person_detect`, `ml_regression`, `ml_yolo_lc`, `ml_yolov8`) need `omv` and OpenMV's `ml` module. The app has `tflite` and `litert`.

## Update

After a new OpenMV version, run the tests. A test that fails is either a real change or a new difference from OpenMV. Look at the OpenMV change before editing the expected values. A new OpenMV test is added by copying it here with a header, and by listing the changes.

Files in `native-bringup/my-overrides/openmv/` with the same name as an OpenMV file replace or patch that file: `apriltag_config.h`, `arm_math.h`, `cmsis_compiler.h`, `cmsis_extension.h`, `fmath.c`, `framebuffer.c`, `framebuffer.h`, `mutex.c`, `mutex.h`, `omv_csi.h`, `omv_cycles.h`, `omv_gpu.h`, `py_gif.c`, `py_mjpeg.c`, `queue.c`, `queue.h`. They do not follow a new OpenMV version by themselves. Compare each one with the new OpenMV file. `native-bringup/patches/openmv/` holds patches to OpenMV files, applied by `apply-overrides.sh`. A patch that no longer applies fails the build.
