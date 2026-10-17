# upy-android

[MicroPython](https://micropython.org/) for Android with an [OpenMV](https://github.com/openmv/openmv)-inspired computer-vision API.

|[![screenshot](doc/pictures/screenshot\_small.jpg)](doc/pictures/screenshot\_big.jpg)|[![street crossing](doc/pictures/street\_crossing.jpg)](https://github.com/koendv/upy-android/raw/refs/heads/main/doc/pictures/street_crossing.mp4)|
|---|---|
| Command screen | Camera screen |

### What upy-android is

upy-android turns an Android phone into a MicroPython machine vision system.

One use case is as a hands-on lab for computer vision courses: camera capture, image processing, AI inference, and result output are all integrated into a single smartphone, requiring no additional hardware.

### Documentation

The documentation comes in two parts:

- README.MD (this document) for MicroPython machine vision users
- [DEVELOPER.md](doc/DEVELOPER.md) for app developers

### Requirements:

- Android 9 or later (API 28)
- 64-bit ARM (`arm64-v8a`)

### Install

Download the APK from [Releases](https://github.com/koendv/upy-android/releases) and install it.

### One minute on-boarding

- Start up the application.
- Give camera permission when asked.
- Notice the four icons: _Command_, _Files_, _Camera_, _Settings_. Choose _Files_.
- In the Files screen, choose _examples_ -> _find\_line\_segments.py_ . 
- A pop-up window appears. Choose _Run_. The display shows fps (frames per second).
- Choose _Camera_. Point the phone camera at objects.
- The camera image is shown; superimposed are line segments in red.
- To stop, choose the icon of a square in the upper right corner.

### Example Script

A script to print the id and center of every [AprilTag](doc/apriltags.pdf) the camera sees.
The **Camera** screen shows the image, with a box around each tag:

```python
import camera, display

cam = camera.Camera(size=(320, 240))
lcd = display.SPIDisplay()

while True:
    img = cam.snapshot()
    for tag in img.find_apriltags():
        img.draw_detection(tag)
        print(tag.id, tag.cx, tag.cy)
    lcd.write(img)
```

More example scripts are in `/examples` on the phone.

### Programming

Programming is in [MicroPython](https://docs.micropython.org/en/latest/), a Python derivative for microcontrollers.
One may program by hand, or AI-assisted _coding by intent_.

#### Programming by hand

The app has a built-in editor: _Files_ -> tap a file -> _Edit_. Tap _run icon_ to run the file. Tap _Command_ to see program text output, _Camera_ to see program bitmap output.

In the _Files_ menu:

- _Open with_: edit file with editor app
- _Share_: send file to app 
- _Import_: file picker for file from other app

From another app:

- tap _Share_ and choose _upy_.

Using ssh Secure Shell:

- In the app: _Settings_ -> _Enable SSH_ and set ssh password. ssh will not work without password.

On the PC:

- `ssh -p 2222 user@<phone-ip>`
- `sftp -P 2222 user@<phone-ip>`

In ssh, type MicroPython code. End with an empty line to run the code.

#### AI-assisted coding by intent

The app has been designed for ai. Just tell the ai coding agent where to find the app and begin coding. 
Describe what you want. The agent writes the code, runs it on the phone and checks the result.

Setup:

- PC with coding agent that can run shell commands, for example Claude Code.
- Android phone with upy-android installed
- usb cable from PC to Android phone

Steps:

- In Android, enable USB debugging: _Settings_ -> _About Phone_ -> tap _Build Number_ 7 times
- In Android, with USB debugging enabled: _Settings_ -> _System_ -> _Developer Options_ select _Stay awake_ and _USB debugging_
- Connect PC and Android with usb cable.
- In upy-android, open **Settings** and enable **adb exec**.
- On the PC, give the agent this prompt:

   > An Android phone is connected over USB. The app content provider runs code over adb. Start with:
   > `adb exec-out content call --uri content://eu.kdvelectronics.upyandroid.exec --method help`
   > and follow what it says.

- When a message appears "Allow USB debugging?" select _Allow_, _Always from this computer_.

The agent learns the API from the phone. A good first question: "Describe the features of this micropython."

Use this pattern:

1. "Do not generate yet, first discuss."
2. Describe what you want.
3. "Is my intent clear?" Repeat until the answer is yes.
4. "Do you have additional questions?"
5. "Do you need additional data?"
6. "Proceed."

For a real example session, see [SAMPLE_SESSION.md](doc/SAMPLE_SESSION.md).

usb debugging over WiFi or mobile networks is also possible; use with caution.

### Modules

| Module | Description |
|---|---|
| `camera` | Grayscale or RGB565. Zoom, torch, exposure, focus point, frame rate. |
| `display` | Shows images in the **Camera** screen of the app. |
| `image` | OpenMV image processing: AprilTags, Data Matrix, circles, rectangles, lines, edges, template matching, HOG, LBP, face and eye detection. |
| `gif`, `mjpeg` | Video recording. |
| `android` | Proximity sensor, save to gallery, share. |
| `location` | GPS and network location. |
| `imu` |  Inertial Measurement Unit: accelerometer and gyroscope. |
| `tflite` | TensorFlow Lite inference on CPU or NNAPI. |
| `litert` | LiteRT inference on the CPU, GPU or NPU, see [NPU](#npu). |
| `ulab` | Arrays, similar to NumPy. |
| `umqtt` | MQTT client, with TLS. |
| `http` | Web requests with HTTPS and HTTP. |
| `zipfile` | Zip archives. |

The API of each module is in [API.md](doc/API.md). On the phone, `help()` gives the same text, for example `import camera; print(camera.help())`.

### NPU

`litert` does TensorFlow Lite inference on the CPU, the GPU or the NPU. The CPU and the GPU work out of the box, and are sufficient for most work. On some processors, for the highest performance, `litert` can run on the NPU.

Before using the GPU or the NPU, run `litert_bench.py` from `/examples` on the phone. The `litert_bench.py` script tests and times the CPU, the GPU and the NPU.

The NPU needs a runtime library, which depends on the phone's processor. Runtimes with a licence that allows redistribution are included in the APK: MediaTek and Google Tensor. A runtime does not guarantee that the NPU works on every phone with such a processor. The Qualcomm runtime has a restrictive licence, so you download the runtime yourself. For Samsung, no ready-made runtime has been found.

- How to [install an NPU runtime](doc/LITERT.md)
- [Known results](doc/NPU.md#known-results)

### Status

The code was written with AI assistance and has had no independent audit. 

| Group | Tests | Result on the Redmi 9 |
|---|---|---|
| Project tests (`tools/run-tests.py`) | 15 | all pass, no known failure |
| MicroPython `basics` (`--micropython`) | 574 | 542 pass, 32 skipped, 0 failed |
| OpenMV unit tests, adapted (`--openmv`) | 78 | all pass |
| **Total** | **667** | **635 pass, 32 skipped, 0 failed** |

The test run covers the modules, MicroPython's and OpenMV's own test suite. Depending on phone, LiteRT runs on CPU, GPU or NPU, see [Known results](doc/NPU.md#known-results).

### Development

Building, testbench and developer notes: [DEVELOPER.md](doc/DEVELOPER.md).

### License

[MIT](LICENSE.md). Upstream files (MicroPython, ulab, and OpenMV) retain their upstream licenses.

