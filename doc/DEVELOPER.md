# Developer Notes

- Uses MicroPython `ports/embed`, not `ports/unix`.
- Two processes: user interface and MicroPython interpreter.
- interface between user interface and interpreter is AIDL.
- interface between interpreter and Android is JNI.
- The MicroPython file system is in the app's private storage.
- `arm64-v8a` only. An arm32 build is possible but out of scope: the release test matrix would double.

## Build

### Local build

Requirements:

- A full JDK 21, with `javac`.
- The Android SDK with the NDK, CMake and platform versions that the build uses. See [`tools/docker/Dockerfile`](../tools/docker/Dockerfile).
- `upy-android/local.properties` with the SDK path, for example `sdk.dir=/home/user/Android/Sdk`.

```bash
export JAVA_HOME=/path/to/jdk-21
cd upy-android
./gradlew :app:assembleDebug
# -> app/build/outputs/apk/debug/app-debug.apk
```

The first build downloads:

- MicroPython, OpenMV, ulab and AprilTag from GitHub (`native-bringup/fetch-upstream.sh`).
- Libraries from Maven Central and Google Maven.

On a slow or filtered network, set a proxy or a mirror for Git and Gradle.

### Docker build

Run from the repo root:

```bash
docker build --target tools -t upy-android-tools -f tools/docker/Dockerfile .   # SDK/NDK install
docker build --target build --build-arg BUILD_TYPE=debug -t upy-android -f tools/docker/Dockerfile .
id=$(docker create upy-android)
docker cp "$id":/output ~/upy-android_apk
docker rm "$id"
```

The Docker build also downloads the base image from Docker Hub.

### New libraries

Gradle dependency locking is on (`upy-android/app/gradle.lockfile`). After adding a library to `build.gradle.kts`, update the lock file for all build types:

```bash
./gradlew :app:dependencies --update-locks <group>:<artifact>
```

## Install on the phone

Enable **USB debugging** on the phone (see [Phone settings](../README.md#phone-settings) in the README). Then:

```bash
cd upy-android/app/build/outputs/apk/debug/
adb install -r app-debug.apk
```

### Xiaomi: installing over USB

Tested on a REDMI Note 15 5G. In **Developer options**, enable **USB debugging**, **USB debugging (Security settings)** and **Install via USB**. Below the **Install via USB** switch is a second item with the same name, **Install via USB >**. Tap this second item. The screen shows "Denied installation of the following apps via USB". If upy-android is in this list, set its switch to off. Otherwise `adb install` of upy-android is refused.

![Xiaomi Developer options, Debugging section](pictures/xiaomi_install_via_usb.png)

## Testing

### adb exec

adb exec runs MicroPython scripts on the phone from a PC. adb exec is a content provider in the app; programmers and AI agents use adb exec for development and testing.

adb exec is remote code execution by design. Anyone with ADB access to the device can run arbitrary MicroPython. Leave adb exec off unless developing.

To enable adb exec: in upy-android, open **Settings** and enable **adb exec**. Then start with:

```bash
adb exec-out content call --uri content://eu.kdvelectronics.upyandroid.exec --method help
```

Run a script from the PC:

```bash
tools/upy-adb script.py
```

`tools/upy-adb` resets the interpreter, runs the file on the phone and prints the output. Without the tool, the script goes base64-encoded in the `run` call:

```bash
adb exec-out content call --uri content://eu.kdvelectronics.upyandroid.exec --method run --arg "$(base64 -w0 script.py)"
```

To stop a running script, for example one with `while True`, run in a second terminal:

```bash
adb exec-out content call --uri content://eu.kdvelectronics.upyandroid.exec --method interrupt
```

Output is limited to 128 KB, a script to about 96 KB. The help page explains all methods (`status`, `run`, `reset`, `interrupt`) and limits. To use adb exec with an AI agent, see [AI-assisted programming](../README.md#ai-assisted-programming) in the README.

The modules `camera`, `display`, `android` (and its submodules), `tflite` and `litert` have a `help()` function, for example `import camera; print(camera.help())`. With these, an agent can learn the API from the phone. When you add, remove or change a method, update the module's `help()` text in the same commit.

`/examples` on the phone has example scripts, for example `find_apriltags.py` and `face_detection.py`. They cover the OpenMV modules (`image`, `gif`, `mjpeg`, ...) that have no `help()`.

### Test scripts

Both scripts need one phone connected, with the app installed and **adb exec** enabled.

- `tools/run-selftests.py` runs this project's own tests (`upy-android/examples/*_selftest`) and compares the output with the `.exp` file next to each test. `--only <name>` runs one test.
- `tools/run-upstream-tests.sh` runs MicroPython's own test suite on the phone, for example `tools/run-upstream-tests.sh -d basics`.

### SSH, SFTP and HTTP

Enable these in **Settings**. Login is with a password.

```bash
ssh -p 2222 user@<phone-ip>      # MicroPython shell
sftp -P 2222 user@<phone-ip>     # only files in the app's file store
```

The HTTP file server is on port 8080, read-only.

- `/media` lists the images and videos that scripts saved to the gallery. `/media/<token>` downloads one.
- `/files/<path>` downloads a file from the app's file store. Enable **private files** in **Settings** first.

Login uses Digest authentication, so the password does not cross the network. Any user name works.

```bash
curl --digest -u user:<password> http://<phone-ip>:8080/media
```

## Upstream code and patches

- The versions of MicroPython, OpenMV, AprilTag, ulab and LiteRT are pinned in [`upy-android/upstream.properties`](../upy-android/upstream.properties).
- `upy-android/native-bringup/vendor/` has unmodified upstream files. `upy-android/native-bringup/my-overrides/` has this project's own files and patched copies of upstream files. A patched copy has a comment "upy-android PATCH" that explains the change.
- The build generates `micropython_embed/` from these two directories. Do not edit `micropython_embed/` by hand: the next build overwrites it.

## Networking

For a more robust and simpler implementation, network protocols are not written in Python on top of sockets.
Sockets are in Android, not MicroPython.
A known good Java, Kotlin, C or C++ library is wrapped as a MicroPython module.

Three layers, for example `umqtt`:

1. MicroPython module: `mqtt_module.cpp`.
2. JNI interface: `mqtt_jni_bridge.cpp`, `mqtt/MqttShim.kt`.
3. External library: HiveMQ MQTT Client.

Protocols that have to be persistent across interpreter resets and crashes, or that need an Activity, are better run in the user interface process, with MicroPython calling them over AIDL.

## Machine learning: tflite, litert

Two independent native modules:

- `tflite`: the classic TensorFlow Lite C API.
- `litert`: standalone LiteRT. It can use the CPU, GPU or NPU with `litert.Accelerator.{CPU,GPU,NPU}`.

See the `help()` of each module for the exact API.

Notes:

- LiteRT is part of the APK. It is not the Google Play variant.
- Both modules take and return plain `array.array`, not `ulab.numpy` arrays.
- Neither module handles quantization (scale, zero point). There is no automatic dequantization.
- The shape is a separate query, `input_shape(i)` and `output_shape(i)`. `run()` does not return shape.

## Version and release

### Version

The app version is set in one place, [`upy-android/version.properties`](../upy-android/version.properties). To change the version, edit that file and commit.

### GitHub release

To publish a release on GitHub: **Actions > upy-android > Run workflow**, tick **release**. The release tag is `v<version>` for a release build and `v<version>-debug-<run>` for a debug build.

### Signing key

If different builds of an app are signed with the same key, upgrading the app keeps the private files.
If different builds of an app are signed with different keys, the app must be uninstalled before installing the new build, losing all private files.

Debug and release APKs are signed with one key, `~/.android/debug.keystore` on the development PC.
Keep a backup of this key offline.

GitHub builds read the key from the repository secret `SIGNING_KEYSTORE`:

```
base64 -w0 ~/.android/debug.keystore | gh secret set SIGNING_KEYSTORE --repo koendv/upy-android
```

Local Docker builds need `--secret id=signing_keystore,src=$HOME/.android/debug.keystore`.

Without the secret, each Docker or GitHub build uses a different random key.
