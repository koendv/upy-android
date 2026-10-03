# Sample session

An AI coding agent programs upy-android: AprilTag detection on the phone, results sent to an MQTT broker on the PC. See [AI-assisted programming](../README.md#ai-assisted-programming) in the README for the setup.

Before starting (not part of the recorded session):
- The phone is on the same Wi-Fi as the PC, connected by USB, with adb exec enabled in the app settings.
- An AprilTag (for example [apriltags.pdf](apriltags.pdf) on a screen) is ready to hold in front of the camera.
- Start the session in an empty directory: `cd ~/src/upy-demo && claude`.

A real session, shortened. Your prompts are unchanged. The agent's replies are shortened, and `[...]` marks a cut. The session started with the setup prompt and "Describe the features of this micropython."

> **You:** Do not generate yet, first discuss. I want to install the Mosquitto MQTT broker on this PC, Ubuntu 24.04, IP 192.168.1.20. The phone must reach it over Wi-Fi.
>
> **Agent:** Here's the plan. Nothing has been installed or changed yet. [...] Mosquitto 2.x accepts connections only from the PC itself until you configure it otherwise, so we'd add a small config file, `/etc/mosquitto/conf.d/lan.conf`, with `listener 1883`. [...] Do you want a password, and do you want TLS? Is ufw active? What will the phone send?
>
> **You:** password, no TLS, no firewall configured, apriltags
>
> **Agent:** [checks the phone's Wi-Fi address and the `umqtt` API] The phone is on Wi-Fi at 192.168.1.73, the same network as the PC. [...] Phone script: back camera, grayscale, a JSON message per frame with tags on `upy/apriltags`, a retained `upy/status` with a last will. [...] Do you need the tag's 3D distance and orientation, or are pixel positions enough? What reads the messages on the PC?
>
> **You:** pixel positions only, mosquitto_sub only.
>
> **Agent:** [writes `apriltag_mqtt.py` and an install script, tests the camera settings on the phone] The frame rate is about 20 fps at both 320×240 and 640×480, so I'll use 640×480. [...] Mosquitto isn't installed yet, because that step needs your sudo password.
>
> **You:** I will run the sudo in another window
>
> **Agent:** [waits for the broker, then tests] I sent one test message from the phone to the broker and it arrived on the PC. A login with the wrong password is refused. The AprilTag script is now running on the phone. [...] Point the phone's back camera at a TAG36H11 tag.
>
> **You:** The camera is pointed at a tag now
>
> **Agent:** The camera is detecting the tag and every detection is reaching the PC. [...]
> `upy/apriltags {"fps": 29.4, "tags": [{"id": 425, "fam": "TAG36H11", "cx": 304, "cy": 187, "rot": 88.3, ...}]}`
> Message rate: capped at 10 per second as planned. [...]
>
> **You:** save the script on the phone as /apriltag_mqtt.py
>
> **Agent:** The script is saved on the phone as `/apriltag_mqtt.py`. I read it back from the phone and it matches. To start it, tap **apriltag_mqtt.py** → **Run** in the **Files** screen.
