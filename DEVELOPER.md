# Developer Notes

- Uses micropython `ports/embed`, not `ports/unix`. 
- Two processes: user interface and micropython interpreter.
- MicroPython filesystem is in application-private storage.
- `arm64-v8a` only. `minSdk 27`, `compileSdk`/`targetSdk 37`.

### ssh server

```bash
ssh -p 8022 user@<android-device>
```

Enable in settings. Authentication password only.

### adb server

Development environment is linux desktop running `claude`, connected via usb to an android tablet. An adb content provider allows executing micropython on the android from the desktop. This is used for development and testbench, both firmware and user scripts.

AI prompt:

> An Android phone running upy-android from http://github.com/koendv/upy-android is connected over USB. The app content provider runs code over adb. Start with:
>`adb exec-out content call --uri content://eu.kdvelectronics.upyandroid.exec --method help`
> and follow what it says.

Each of the upy-specific modules (`csi`, `display`, `android` and its submodules, `tflite`, `litert`) has a `help()` function, e.g. `import csi; print(csi.help())`, so an AI driving the phone this way can learn the API without repo access. When adding, removing, or changing a method's signature/behavior, update that module's `help()` text in the same commit.

`/examples` on the device has reference scripts (e.g. `find_apriltags.py`, `face_detection.py`) covering the OpenMV modules (`image`, `gif`, `mjpeg`, ...) that don't have a `help()`.

## machine learning: tflite, litert

Two independent native modules, not an OpenMV `ml`/`py_ml.c` compatibility
layer (OpenMV's `ml` module has a restrictive license; this port never
mirrors its API, so there's nothing to clean-room). Both take/return
`ulab.numpy` ndarrays directly -- no quantization/scale/zero_point
handling by either module, no automatic dequantization: a script that
needs those does its own math on the raw ndarray.

- `tflite`: classic TensorFlow Lite C API, CPU-only, built from
  LiteRT's own copy of the TFLite source (see `app/src/main/cpp/tflite/README.md`
  for why that source, not upstream TensorFlow).
- `litert`: standalone LiteRT (bundled in the APK, not the
  Play-Store-delivered variant), accelerator-capable via
  `litert.Accelerator.{CPU,GPU,NPU}`.

Both link the same `libLiteRt.so` -- one copy of TFLite in the process,
not two. See each module's own `help()` for the exact API.

### version

The version is set in one place, [`upy-android/version.properties`](upy-android/version.properties):

```
version=0.2
```

To bump version, edit that line and commit.

### github workflow

To publish a release on github: `Actions > upy-android > Run workflow`, tick "release". The release is tagged `v<version>` for a release build, `v<version>-debug-<run>` for a debug build.

### docker build

Run from the repo root:

```bash
docker build --target tools -t upy-android-tools -f tools/docker/Dockerfile .   # SDK/NDK install
docker build --target build --build-arg BUILD_TYPE=debug -t upy-android -f tools/docker/Dockerfile .
id=$(docker create upy-android)
docker cp "$id":/output ~/upy-android_apk
docker rm "$id"
```

### local build

```bash
export JAVA_HOME=/path/to/jdk-21
cd upy-android
./gradlew :app:assembleDebug
# -> app/build/outputs/apk/debug/app-debug.apk
```

