---
name: upy-android
description: Run MicroPython scripts on an Android phone through the upy-android app (package eu.kdvelectronics.upyandroid) over adb. Use when the user wants to use the phone's camera, IMU, GPS, proximity sensor, run TFLite/LiteRT models, or execute Python on the phone via "adb exec".
---

# upy-android over adb

The app embeds MicroPython (OpenMV-style) and exposes it through an exported
ContentProvider. Source and docs: this repo / https://github.com/koendv/upy-android

This skill deliberately does not list modules or methods: the app documents itself,
and a copied list goes stale. **Ask the phone what it has now** (step 1 below).

## Workflow

```bash
S=.claude/skills/upy-android/upy-run     # path relative to the repo root
$S status                       # engine connected? busy? screen on? camera available?
$S modules                      # step 1: current module list + every module's help(): the API reference
echo 'print(1+1)' | $S run -    # step 2: run a script (file or stdin)
$S run -t 60 --reset prog.py    # timeout 60 s, fresh interpreter first
$S interrupt | reset | help     # stop a script / clear globals / full protocol text
$S input "yes"                  # answer input() / sys.stdin of a running script
```

- Write scripts against what `modules` printed, not against memory. Any module may be added, renamed or removed.
- `run` and `modules` warn on stderr if the app's `protocol:` differs from the one this helper was written for
  (4). If so, read `$S help` (it is the authoritative protocol text) before trusting the notes below.
- Replies look like `Result: Bundle[{output=..., status=ok|exception|timeout}]`; errors like `Result: Bundle[{error=<code>}]`.
- Raw form without the helper: `adb exec-out content call --uri content://eu.kdvelectronics.upyandroid.exec --method run --arg <base64 script> --extra timeout:i:30`.
  The authority is `eu.kdvelectronics.upyandroid.exec` (not `adb-exec`).

## Preconditions

- `adb devices` shows the phone; set `ANDROID_SERIAL` if there are several.
- App Settings > "Enable adb exec" must be on, else `error=disabled`.
  `adb shell content query --uri content://eu.kdvelectronics.upyandroid.exec` prints the help even when it is off.
- Camera needs the screen on and unlocked, the app in the foreground, and the camera permission (`status` says why not).
  Camera and location prompt for permission on first use.
- Errors: `busy` (another script, maybe from the UI: `interrupt`), `disconnected` (engine down).

## Execution model (as of protocol 4)

- Each `run` executes one whole script; no REPL. Globals persist between runs; `--reset` clears them.
- `run` blocks until done or the timeout. A script waiting on stdin shows `input: waiting` in `status`; answer from a second call with `input`.
- Limits: output 128 KB (longer is truncated), script ~48 KB (send big data in chunks).
- `/` is the app's private storage; `/examples` holds sample scripts. Move files with `http.download` / `http.put`
  against a server on the PC (`python3 -m http.server`), or SFTP on port 2222 once SSH and a password are set in the app.

## Tips

- Camera, IMU, location and the display use real hardware and may show prompts: use them only when the user asks.
- To see the phone screen: `adb exec-out screencap -p > shot.png`.
