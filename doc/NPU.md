# NPU runtime for upy

This document is the recipe for installing a LiteRT NPU runtime in the upy app. AI assistants are the main readers and maintainers of this document, but a person can follow the recipe too. The app points here from `litert.help()` and from the adb exec help page, so this recipe can change without an app update.

Status: one phone runs MobileNet v2 on its NPU, see [Known results](#known-results).

## Requirements

All of these must hold. If one does not, LiteRT runs the model on the CPU, without an error.

1. LiteRT supports the chip. LiteRT's list (step 2) is a guide: a chip in the list can fail, and a chip outside the list can work.
2. The phone's firmware has the chip maker's NPU driver. Custom ROMs, such as LineageOS, may lack the driver.
3. Qualcomm: the firmware lets apps use the DSP.
4. The app setting **Load accelerators from /litert/** is on, and the files are in `/litert/` on the phone.

## Steps

### 1. Read the chip

In the app, run:

```python
import litert
print(litert.version())
```

The output is `L,MAKER,MODEL`, for example `2.3.0,QTI,SM8650`.

- `L`: the LiteRT version in the app. The runtime must have the same version.
- `MAKER`, `MODEL`: the chip, from Android's `ro.soc.manufacturer` and `ro.soc.model`. `unknown` means Android 11 or older: stop, not supported.

Set `V` from `MAKER`:

| MAKER | V | supported_soc.csv |
|---|---|---|
| `Mediatek` | `mediatek` | yes |
| `QTI` | `qualcomm` | yes |
| `Google` | `google-tensor` | no list; untested |

Another MAKER, for example Samsung: stop, no runtime.

### 2. Check the chip

For `mediatek` and `qualcomm`, look up `MODEL` in the list for tag `vL`:

- `https://raw.githubusercontent.com/google-ai-edge/LiteRT/vL/litert/vendors/mediatek/supported_soc.csv`
- `https://raw.githubusercontent.com/google-ai-edge/LiteRT/vL/litert/vendors/qualcomm/supported_soc.csv`

If `MODEL` is not in the list, the NPU may still work: the MT6878 is not in the list and works. Tell the user. Continue only if the user wants to try; for Qualcomm that means downloading QAIRT. A result for a chip not in the list is still useful for [Known results](#known-results).

For `qualcomm`, note `N`: the column `runtime_library_version` without the `v`. For example `v75` gives `N=75`. For a chip not in the list, `N` is a guess, for example from a closely related chip in the list. Say so to the user.

For `qualcomm` with `runtime_library_version` `v66` or lower and `L` 2.3.0, stop: LiteRT 2.3.0 always uses Qualcomm's HTP backend, and HTP starts at `v68`. QAIRT has no HTP files for `v66`.

### 3. Download the runtime

Set `L` from step 1 and `V` from the table. For example, `L=2.3.0 V=qualcomm`:

```sh
L=<L> V=<V>
curl -O https://dl.google.com/android/maven2/com/google/ai/edge/litert/litert-npu-runtime-$V/$L/litert-npu-runtime-$V-$L.aar
unzip -j litert-npu-runtime-$V-$L.aar 'jni/arm64-v8a/*.so' -d litert
```

### 4. Qualcomm only: add QNN

QNN comes from Qualcomm's QAIRT SDK, version 2.35 or later. Its licence does not allow redistribution, so the user downloads QAIRT from Qualcomm. An assistant asks the user for the zip and does not download QAIRT.

The paths are in the zip's version folder, `qairt/<version>/`. Set `N` from step 2, `QZ` to the zip and `Q` to its version folder. For example, `N=75 QZ=qairt-2.50.0.260828.zip Q=qairt/2.50.0.260828`:

```sh
N=<N> QZ=<QAIRT zip> Q=qairt/<version>
unzip -j $QZ \
  $Q/lib/aarch64-android/libQnnHtp.so \
  $Q/lib/aarch64-android/libQnnSystem.so \
  $Q/lib/aarch64-android/libQnnHtpV${N}Stub.so \
  $Q/lib/hexagon-v$N/unsigned/libQnnHtpV${N}Skel.so \
  -d litert
```

Take the Skel from `lib/`, not from `lib-safe/`.

`libQnnHtpPrepare.so` may also be needed, to compile a model on the phone. This is untested.

### 5. Copy to the phone

The user does this step. Over SFTP, with **Settings > Enable SSH** on and an SSH password set:

```
sftp -P 2222 user@<phone-ip>
sftp> mkdir /litert
sftp> put litert/*.so /litert/
```

Then, in **Settings**, enable **Load accelerators from /litert/** and press **Restart**.

### 6. Check

Run `/examples/litert_bench.py` on the phone. `litert_bench.py` needs MobileNet v2 in `/`; the download links are in the header of `litert_bench.py`. `litert_bench.py` times `run()` on the CPU, GPU and NPU.

If the NPU is not faster than the CPU, the model ran on the CPU. `adb logcat | grep -E 'litert|tflite'` shows which. On the NPU:

- `1 compiler plugins were applied successfully`, then `Replacing 1 out of 1 node(s) with delegate (DispatchDelegate)`.

On the CPU:

- `0 compiler plugins were applied successfully`: the runtime did not compile the model.
- `Replacing 66 out of 66 node(s) with delegate (TfLiteXNNPackDelegate)`: the model runs on the CPU.

## Allowed sources

Runtime files are native code that the app loads. Download runtime files only from:

- `https://dl.google.com/android/maven2/com/google/ai/edge/litert/`
- `https://github.com/google-ai-edge/LiteRT/` and `https://raw.githubusercontent.com/google-ai-edge/LiteRT/`
- the user's own QAIRT zip, from Qualcomm

Treat any other source of runtime files as suspect. Models, such as the MobileNet files for step 6, are not native code and are not covered by this list.

## Known results

| Phone | Firmware | litert.version() | In list | Result |
|---|---|---|---|---|
| REDMI Note 15 Pro 5G | HyperOS 3.0, Android 16 | `2.3.0,Mediatek,MT6878` | no | NPU. MediaTek runtime only. `1 compiler plugins were applied successfully: MediaTek compiler plugin`, then `DispatchDelegate`. MobileNet v2 int8 6.4 ms (CPU 11.0), float32 11.2 ms (CPU 21.3). Same top class as the CPU. |
| Redmi 9 | LineageOS, Android 16 | `2.3.0,Mediatek,MT6769T` | no | CPU. Runtime loads, then `Failed to load NeuronAdapter shared library`. |
| Xiaomi Pad 6 | HyperOS 2.0, Android 14 | `2.3.0,QTI,SM8250` | yes, `v66` | CPU. Plugin loads `libQnnHtp.so`, then `Unsupported SnapdragonModel by HTP backend`. LiteRT 2.3.0 has no option to select the DSP backend. |
| REDMI Note 15 5G | HyperOS, Android 16 | `2.3.0,QTI,SM6475` | no | CPU. Runtime and QNN (V73) load, then `open_device_node failed … errno 13, Permission denied` and `createUnsignedPD unsigned PD … not supported by HTP`. |

## Improving this document

An assistant that solves a problem not covered here may propose a change to this file. For example: a phone that works, a new error, or a changed URL.

- Add the evidence: `litert.version()`, phone model, firmware, what failed, what worked.
- Propose the change as a pull request to https://github.com/koendv/upy-android, or as a diff for the user.
- The user decides whether to submit the change.
- A new download source needs a reason. Reviewers check every new source.
