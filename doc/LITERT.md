# Installing an NPU runtime

`litert` runs on the CPU, the GPU or the NPU, as the script chooses with `litert.Options(accelerator=...)`. The CPU and the GPU work out of the box. The NPU needs a runtime for the phone's chip, which the user installs. LiteRT supports the NPU of some phones, not all.

Download the runtime with an AI assistant. Connect the phone over USB with USB debugging on, enable **Settings > Enable adb exec**, and give an AI assistant that can run commands and browse the web this prompt:

```
My Android phone runs the MicroPython app upy. Start with:
  adb exec-out content call --uri content://eu.kdvelectronics.upyandroid.exec --method help
and download the NPU runtime for my phone.
```

The assistant puts the runtime in a folder `litert` on the PC. For a Qualcomm chip, the assistant asks for Qualcomm's QAIRT SDK, which the user downloads from Qualcomm.

Copy the runtime to the phone:

1. In the app, **Settings > Enable SSH**, and set an SSH password.
2. On the PC:
   ```
   sftp -P 2222 user@<phone-ip>
   sftp> mkdir /litert
   sftp> put litert/*.so /litert/
   ```
3. In **Settings**, enable **Load accelerators from /litert/** and press **Restart**.
4. Run `/examples/litert_bench.py` in the app, or ask the AI assistant to run `litert_bench.py`. If the MobileNet models are missing, the script downloads the MobileNet models from github into `/rom`. If the NPU is not faster than the CPU, the runtime did not load and LiteRT runs on the CPU.

For details, see [NPU](NPU.md).
