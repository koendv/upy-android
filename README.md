# upy-android

MicroPython on Android, with a subset of [OpenMV](https://github.com/openmv/openmv)'s own modules. Single APK, two processes.

## What it is

[![screenshot](doc/screenshot_small.jpg)](doc/screenshot_big.jpg)

MicroPython (`ports/embed`, not `ports/unix`) embedded via JNI in an Android app.
A bind-only `:engine` Service runs the interpreter; the main process runs the Compose UI.
Communicates over AIDL, not a REPL byte protocol.

Not a Termux wrapper.

No USB/hardware board required.

## Features

- MicroPython REPL with output streaming and interrupt
- File explorer, text editor, REPL.
- Camera and display modules (`csi`, `display`) backed by Camera2/NDK and `ANativeWindow`; camera screen in-app
- `android` module for phone API
- VFS rooted at app-private storage
- OpenMV script compatibility (`csi`, `image`, `ml`/`tf`, `gif`, `mjpeg`, `ulab`, ...) --
  real OpenMV scripts run largely unmodified, growing as real scripts need more
- 32 MB micropython heap, settable.

## Design

- Single APK
- Two OS processes: UI (`main`), interpreter (`:engine`)
- micropython `ports/embed` + JNI, one reused worker thread in `:engine`
- AIDL: `exec`/`interrupt`/`reset`/`setOutputListener`/`setDisplaySurface`
- Idle/lazy Service bind; explicit `reset()` does in-process `mp_deinit()`+`mp_embed_init()`, not unbind/rebind
- Crash isolation verified: `kill -9` on `:engine` leaves UI intact
- arm64-v8a only. minSdk 27, compile/targetSdk 37

## Build Notes

Requirements: JDK 21, Android SDK 37, NDK 30, Gradle wrapper 9.7.1.

### GitHub Actions

fork, run the upy-android workflow, download the APK artifact.

### Docker target build

```bash
# once: SDK, NDK, CMake
docker build --target tools -t upy-android-tools -f tools/docker/Dockerfile .
# every build (incremental)
docker build --target build --build-arg BUILD_TYPE=debug -t upy-android -f tools/docker/Dockerfile .
id=$(docker create upy-android)
docker cp "$id":/output/upy-debug.apk ~/Downloads/
docker rm "$id"
```

### Local build

Linux only; other platforms use Docker. Needs git, python3, make, gcc, curl, unzip, network on the first build.

```bash
cd upy-android
export JAVA_HOME=/path/to/jdk-21
./gradlew assembleDebug
# -> app/build/outputs/apk/debug/app-debug.apk
```

The build fetches MicroPython, OpenMV, ulab and LiteRT at the versions pinned in `upstream.properties`, and regenerates the native sources when their inputs change.

Exception: LiteRT's C API headers are committed in `upy-android/app/src/main/cpp/litert/include/`. Upgrading LiteRT is a manual step, see [`litert/README.md`](upy-android/app/src/main/cpp/litert/README.md).

### Install / run

```bash
adb install app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n eu.kdvelectronics.upyandroid/.MainActivity
```

CAMERA permission is requested on first launch. Camera permission errors surface as OSError(errno.EACCES, ...) if denied.

## OpenMV

Compiles. Measured ~25 FPS at resolution (320, 240) on a Xiaomi Redmi Note 15 device running [`lcd_shield.py`](https://github.com/openmv/openmv/blob/master/scripts/examples/50-OpenMV-Boards/60-Shields/60-LCD-Shield/lcd_shield.py)

## Android

`csi.CSI().framesize_list()` returns a list of image resolutions the Android phone camera supports.  When setting `framesize()` use a resolution in this list.

In micropython, the `android` module gives access to android devices:

- motion sensor
- torch
- proximity sensor
- zoom
- camera, front or back

## Repo Layout

- `upstream/` upstream git clones: micropython, openmv
- `native-bringup/` overrides for openmv, micropython
- `app/src/main/cpp/micropython_embed/` generated from `my-overrides/` by `apply-overrides.sh`
- `app/src/main/cpp/*.{cpp,h}` own NDK modules

### Status

Working prototype. Largely generated with AI assistance, not fully audited. Use with caution.

### License

[MIT](LICENSE.md). Third-party attribution in [NOTICE](upy-android/NOTICE.html).

MicroPython, ulab, and OpenMV-derived files retain their upstream licenses.
