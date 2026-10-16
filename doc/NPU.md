# NPU runtime for upy

This document is the recipe for installing a LiteRT NPU runtime in the upy app. AI assistants are the main readers and maintainers of this document, but a person can follow the recipe too. The app points here from `litert.help()` and from the adb exec help page, so this recipe can change without an app update.

Status: one phone runs MobileNet v2 on its NPU; Google Tensor is untested. See [Known results](#known-results).

## Built-in runtimes

The runtimes for MediaTek (`MAKER` `Mediatek`) and Google Tensor (`MAKER` `Google`) are built into the app. The text of `litert.help()` says so: "runtimes for MediaTek and Google Tensor are built in". For these chips with such an app, steps 3 to 5 are not needed: go to step 6.

An older app does not say so in `litert.help()`: follow all the steps. Qualcomm and all other chips need steps 3 to 5 with every app.

## Requirements

All of these must hold. If one does not, LiteRT runs the model on the CPU, without an error.

1. LiteRT supports the chip. LiteRT's list (step 2) is a guide: a chip in the list can fail, and a chip outside the list can work.
2. The phone's firmware has the chip maker's NPU driver. Custom ROMs, such as LineageOS, may lack the driver.
3. Qualcomm: the firmware lets apps use the DSP.
4. The NPU runtime is in the app: built in (MediaTek, Google Tensor), or the app setting **Load accelerators from /litert/** is on and the files are in `/litert/` on the phone.

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

Another MAKER: stop, no runtime.

Samsung is one such MAKER, with LiteRT support for the Exynos 2500 and 2600 only. There is no runtime on Maven or in the LiteRT releases. The compiler plugin and the dispatch library (`libLiteRtCompilerPlugin_Samsung.so`, `libLiteRtDispatch_Samsung.so`) have to be built from LiteRT's source. Compiling on the phone also needs Samsung's Exynos AI LiteCore SDK, with 64 MB of arm64 libraries. The licence file of that SDK is DRM-protected, so redistribution is unclear. The user downloads the SDK from Samsung; an assistant does not. Not tested.

References: [LiteRT guide for Samsung](https://developers.google.com/edge/litert/next/samsung), [LiteRT source for Samsung](https://github.com/google-ai-edge/LiteRT/tree/v2.3.0/litert/vendors/samsung), [Samsung Exynos AI LiteCore SDK](https://soc-developer.semiconductor.samsung.com/global/development/ai-litecore).

### 2. Check the chip

For `mediatek` and `qualcomm`, look up `MODEL` in the list for tag `vL`:

- `https://raw.githubusercontent.com/google-ai-edge/LiteRT/vL/litert/vendors/mediatek/supported_soc.csv`
- `https://raw.githubusercontent.com/google-ai-edge/LiteRT/vL/litert/vendors/qualcomm/supported_soc.csv`

If `MODEL` is not in the list, the NPU may still work: the MT6878 is not in the list and works. Tell the user. Continue only if the user wants to try; for Qualcomm that means downloading QAIRT. A result for a chip not in the list is still useful for [Known results](#known-results).

For `qualcomm`, note `N`: the column `runtime_library_version` without the `v`. For example `v75` gives `N=75`. For a chip not in the list, `N` is a guess, for example from a closely related chip in the list. Say so to the user.

For `qualcomm` with `runtime_library_version` `v66` or lower and `L` 2.3.0, stop: the Kotlin API of LiteRT 2.3.0, which the app uses, always uses Qualcomm's HTP backend, and HTP starts at `v68`. QAIRT has no HTP files for `v66`. The C and C++ APIs have a `backend` option with the value `dsp`. A `v66` chip would then need the QAIRT files `libQnnDsp.so`, `libQnnDspV66Stub.so` and `libQnnDspV66Skel.so`. The app does not use these APIs, and this is not tested.

### 3. Download the runtime

Set `L` from step 1 and `V` from the table. For example, `L=2.3.0 V=qualcomm`:

```sh
L=<L> V=<V>
curl -O https://dl.google.com/android/maven2/com/google/ai/edge/litert/litert-npu-runtime-$V/$L/litert-npu-runtime-$V-$L.aar
unzip -j litert-npu-runtime-$V-$L.aar 'jni/arm64-v8a/*.so' -d litert
```

### 4. Qualcomm only: add QNN

QNN comes from Qualcomm's QAIRT SDK, version 2.35 or later. Its licence does not allow redistribution, so the user downloads QAIRT from Qualcomm. An assistant asks the user for the zip and does not download QAIRT.

LiteRT pins the QAIRT version: `QAIRT_URL` in `ci/tools/python/vendor_sdk/qualcomm/setup.py` at tag `vL` of the LiteRT repository. For LiteRT 2.3.0, the pinned version is 2.50.0.260828. The download needs no login, and a plain `wget` works (2.6 GB):

```sh
wget https://softwarecenter.qualcomm.com/api/download/software/sdks/Qualcomm_AI_Runtime_Community/All/2.50.0.260828/v2.50.0.260828.zip
```

For another version, replace both occurrences of the version in the URL. The URL of LiteRT 2.2.0 had the same pattern.

The paths are in the zip's version folder, `qairt/<version>/`. Set `N` from step 2, `QZ` to the zip and `Q` to its version folder. For example, `N=75 QZ=v2.50.0.260828.zip Q=qairt/2.50.0.260828`:

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

Then, in **Settings**, enable **Load accelerators from /litert/** and press **Restart**. With the setting on, the app uses the runtime in `/litert/` instead of the built-in runtimes.

After any later change to the files in `/litert/`, press **Restart** again. LiteRT keeps state from a failed attempt, and once a second attempt in the same process crashed the engine (SIGSEGV in the Qualcomm plugin).

### 6. Check

Run `/examples/litert_bench.py` on the phone. `litert_bench.py` needs MobileNet v2 in `/`. If the models are missing, the script offers to download them from GitHub (18 MB, SHA-256 checked) and asks first: answer `y`. A run from adb answers with the adb `input` method (see `--method help`). `litert_bench.py` times `run()` on the CPU, GPU and NPU.

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
| REDMI Note 15 Pro 5G | HyperOS 3.0, Android 16 | `2.3.0,Mediatek,MT6878` | no | NPU. MediaTek runtime only. `1 compiler plugins were applied successfully: MediaTek compiler plugin`, then `DispatchDelegate`. MobileNet v2 int8 6.4 ms (CPU 11.0), float32 11.2 ms (CPU 21.3). Same top class as the CPU. With the built-in runtime of app 0.9 (release build) and no files in `/litert/`: the same log lines, MobileNet v2 int8 6.9 ms (CPU 10.9, GPU 17.0). |
| Redmi 9 | LineageOS, Android 16 | `2.3.0,Mediatek,MT6769T` | no | CPU. Runtime loads, then `Failed to load NeuronAdapter shared library`. The GPU gives the right classes for MobileNet v2 float32 and int8, but is not faster than the CPU (float32 CPU 62 ms, GPU 55 to 77 ms; int8 CPU 35 ms, GPU 78 ms). The same phone model on MIUI is the next row: there the GPU is faster for float32 and wrong for int8. |
| Redmi 9 (M2004J19C) | MIUI 12.0.2 (V12.0.2.0.QJCEUVF), Android 10 | `2.3.0,unknown,unknown` | no | CPU and GPU run, NPU not available: `0 compiler plugins were applied successfully`, and the model runs on the CPU (XNNPACK). On Android 10 `litert.version()` does not report the chip (Android 11 or older, see step 1). MobileNet v2 float32: CPU 72 ms and GPU 31 ms, both give the right classes. int8: CPU 76 ms, right classes; the GPU gives wrong classes (goldfinch, toilet tissue, scuba diver) where the CPU gives military uniform, suit, Windsor tie. The same phone model with LineageOS (the row above) gets the right classes on the GPU, so the cause is probably the firmware or its GPU driver: Android 10 and MIUI 12 against Android 16 and LineageOS. Not investigated further. The two also differ in what `litert.version()` reports (the chip with LineageOS, not on Android 10) and in whether the NPU runtime is tried (it is with LineageOS, and fails to load NeuronAdapter; not here). The timings are not comparable: this phone was busy compiling an app in the background. `tests/litert_mobilenet.py` fails on this phone, on the int8 GPU run. Use the CPU on this phone. |
| Xiaomi Pad 6 | HyperOS 2.0, Android 14 | `2.3.0,QTI,SM8250` | yes, `v66` | CPU. Plugin loads `libQnnHtp.so`, then `Unsupported SnapdragonModel by HTP backend`. The Kotlin API of LiteRT 2.3.0 has no option to select the DSP backend. |
| REDMI Note 15 5G | HyperOS, Android 16 | `2.3.0,QTI,SM6475` | no | CPU. Runtime and QNN (V73) load, then `open_device_node failed … errno 13, Permission denied` and `createUnsignedPD unsigned PD … not supported by HTP`. |

## Improving this document

An assistant that solves a problem not covered here may propose a change to this file. For example: a phone that works, a new error, or a changed URL.

- Add the evidence: `litert.version()`, phone model, firmware, what failed, what worked.
- Propose the change as a pull request to https://github.com/koendv/upy-android, or as a diff for the user.
- The user decides whether to submit the change.
- A new download source needs a reason. Reviewers check every new source.
