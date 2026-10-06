# upy-android

[MicroPython](https://micropython.org/) for Android with an [OpenMV](https://github.com/openmv/openmv)-inspired computer-vision API.

|[![screenshot](doc/pictures/screenshot\_small.jpg)](doc/pictures/screenshot\_big.jpg)|[![street crossing](doc/pictures/street\_crossing.jpg)](https://github.com/koendv/upy-android/raw/refs/heads/main/doc/pictures/street_crossing.mp4)|
|---|---|
| Command screen | Camera screen |

### What upy-android is

upy-android turns an Android phone into a camera vision module. You program the phone in MicroPython.

A phone has advantages over a microcontroller camera board:

- a better camera
- a faster CPU
- a GPU and NPU for neural networks
- Wi-Fi, a screen and a battery

### Install

Download the APK from [Releases](https://github.com/koendv/upy-android/releases) and install it.

Requirements: Android 8.1 or later (API 27), 64-bit ARM (`arm64-v8a`).

### Phone settings

These settings depend on the phone brand. The names can be different on your phone.

- **Install the APK.** Allow **Install unknown apps** for your browser or file manager.
- **Keep scripts running.** Some phones stop apps in the background. For upy-android, set battery usage to **No restrictions** and allow **Autostart** if your phone has this setting. The camera only works while upy-android is on the screen.
- **USB debugging.** Needed for adb. Open **Settings > About phone** and tap **Build number** 7 times. Then enable **USB debugging** in **Developer options**.

### One minute on-boarding

Start up the application.
Give camera permission when asked.
Notice the four icons: _Command_, _Files_, _Camera_, _Settings_. Choose _Files_.
In the Files screen, choose _examples_ -> _find\_line\_segments.py_ . 
A pop-up window appears. Choose _Run_. The display shows fps (frames per second).
Choose _Camera_. Point the phone camera at objects.
The camera image is shown; superimposed are line segments in red.
To stop, choose the icon of a square in the upper right corner.

### Example

This script prints the id and center of every [AprilTag](doc/apriltags.pdf) the camera sees. The **Camera** screen shows the image, with a box around each tag:

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

### Connecting

The phone sends results over Wi-Fi or USB:

- MQTT (`umqtt`, with TLS). For example, to an MQTT broker or an ESP32.
- SSH shell on port 2222. Password login.
- HTTP file server on port 8080, read-only.
- adb over USB. Run scripts from a PC.

SSH, HTTP, adb:

- enable and set passwords in the Settings screen.
- default is off

### Programming

Distinguish between manual coding, and AI-assisted "coding by intent".

#### Programming by hand

- **Built-in editor.** Open the **Files** screen and tap a file. Tap **Edit** to open the editor. The editor has syntax highlighting, undo, redo, run and save. Tap **Run** to run the file. The output is in the **Command** screen.
- **Other editor apps.** Open the **Files** screen, tap a file and tap **Open with**. Choose an editor app. If the editor app can write back, **Save** writes the file back to upy-android.
- **Files from other apps.** In another app, tap **Share** and choose upy-android. The file is saved in `/` of the upy-android file store. The **Import** button in the **Files** screen does the same with a file picker.
- **SSH.** Enable SSH in **Settings**. Then run `ssh -p 2222 user@<phone-ip>` on your PC. You get a Python shell. Type code, then an empty line to run the code.
- **SFTP.** Enable SSH in **Settings**. Then use `sftp -P 2222 user@<phone-ip>`, `scp -P 2222`, or a program such as FileZilla or WinSCP. SFTP can only access the upy-android file store. This is the same `/` as in MicroPython.

#### AI-assisted programming

An AI coding agent on your PC can write and run scripts on the phone. Any agent that can run shell commands works, for example Claude Code. You describe what you want. The agent writes the code, runs it on the phone and checks the result.

Setup:

1. Connect the phone to the PC with a USB cable. Enable USB debugging (see [Phone settings](#phone-settings)).
2. In upy-android, open **Settings** and enable **adb exec**.
3. Give the agent this prompt:

   > An Android phone is connected over USB. The app content provider runs code over adb. Start with:
   > `adb exec-out content call --uri content://eu.kdvelectronics.upyandroid.exec --method help`
   > and follow what it says.

The agent reads the help page and the `help()` of each module. The agent learns the API from the phone. A good first question: "Describe the features of this micropython."

Agree on the plan before the agent writes code. Use these steps:

1. "Do not generate yet, first discuss."
2. Describe what you want.
3. "Is my intent clear?" Repeat until the answer is yes.
4. "Do you have additional questions?"
5. "Do you need additional data?"
6. "Proceed."

For a real example session, see [doc/SAMPLE_SESSION.md](doc/SAMPLE_SESSION.md).

### Modules

| Module | Description |
|---|---|
| `camera` | Camera, on CameraX. Grayscale or RGB565. Images upright as the phone is held. Zoom, torch, exposure, focus point, frame rate. |
| `display` | Shows images in the **Camera** screen of the app. |
| `image` | OpenMV image processing: AprilTags, Data Matrix, circles, rectangles, lines, edges, template matching, HOG, LBP, face and eye detection. |
| `gif`, `mjpeg` | Video recording. |
| `android` | Proximity sensor, accelerometer, gyroscope, location, save to gallery, share. |
| `tflite` | TensorFlow Lite inference on the CPU, or with Android NNAPI (`tflite.set_nnapi(True)`). |
| `litert` | LiteRT inference on the CPU, GPU or NPU. |
| `ulab` | Arrays, similar to NumPy. |
| `umqtt` | MQTT client, with TLS. |

The modules `camera`, `display`, `android`, `tflite` and `litert` have a `help()` function. For example: `import camera; print(camera.help())`.

### Performance

Phone: REDMI Note 15 5G, Android 16. Grayscale images.

| Function | Resolution | Time per frame | Frame rate |
|---|---|---|---|
| `find_apriltags()` | 640×480 | 9 ms | 31 fps (camera limit) |
| `find_line_segments()` | 320×240 | 16 ms | 25 fps (camera limit) |
| `find_line_segments()` | 640×480 | 82 ms | 12 fps |

For `find_apriltags()`, one AprilTag was in view. The time of `find_line_segments()` depends on the image.

### Status

Working prototype. Programmed with AI assistance, not fully audited. Moderate your expectations. Use with caution.

### Development

Building, the adb test setup and other developer notes: [doc/DEVELOPER.md](doc/DEVELOPER.md).

### License

[MIT](LICENSE.md). MicroPython, ulab, and OpenMV-derived files retain their upstream licenses.
