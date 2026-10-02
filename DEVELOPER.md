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

Each of the upy-specific modules (`csi`, `display`, `android` and its submodules) has a `help()` function, e.g. `import csi; print(csi.help())`, so an AI driving the phone this way can learn the API without repo access. When adding, removing, or changing a method's signature/behavior, update that module's `help()` text in the same commit.

`/examples` on the device has reference scripts (e.g. `find_apriltags.py`, `face_detection.py`) covering the OpenMV modules (`image`, `gif`, `mjpeg`, ...) that don't have a `help()`.

## ml machine learning

The `ml` (machine learning) is a clean-room rewrite of the OpenMV `ml` module.

The OpenMV `ml` (machine learning) module has a restrictive license. 

The module was rewritten using two AI agents, one agent to write the specification, another to write the source. This is similar to Phoenix Technologies' 1984 PC BIOS clean room development. 

If your use is commercial, assume this does not protect you and get legal counsel.

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

