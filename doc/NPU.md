# NPU runtime for upy

This document is the recipe for installing a LiteRT NPU runtime in the upy app. AI assistants are the main readers and maintainers of this document, but a person can follow the recipe too. The app points here from `litert.help()` and from the adb exec help page, so this recipe can change without an app update.

An accelerator is usable on a phone only if step 7 passes on that phone. Step 7 checks one model: also compare the output of your own model on the accelerator with its output on the CPU. Every result in this document belongs to one LiteRT version and one firmware: a result does not carry over to another version, so run step 7 again after an app update.

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
- `MAKER`, `MODEL`: the chip, from Android's `ro.soc.manufacturer` and `ro.soc.model`. `unknown` means Android 11 or older: there is no NPU runtime for this phone, stop here. The CPU and GPU still work, but check the GPU result (step 7).

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

### 7. Check the result

Speed is not enough: an accelerator can be fast and give wrong results. For example, with LiteRT 2.3.0 on the Redmi 9 with MIUI 12 the int8 model runs on the GPU in 39 ms and gives goldfinch where the CPU gives military uniform.

Run `tests/litert_mobilenet.py` from the repository, on the PC, with the phone connected over USB and **Settings > Enable adb exec** on and Wi-Fi on:

```sh
tools/run-tests.py --only litert_mobilenet
```

The phone downloads MobileNet v2, float32 and int8, and a photo of Grace Hopper, and checks their SHA-256. Every accelerator must give the same top classes as the CPU: 653 military uniform, 835 suit, 907 Windsor tie (the 1001 labels of TensorFlow's `ImageNetLabels.txt`, `background` first). The test fails on a wrong result and prints SKIP when the phone is offline. An accelerator that fails the test is not usable on this phone with this LiteRT version: use the CPU, or another accelerator that passes. Add the outcome as a row of the [Known results](#known-results) table.

### The GPU backend

`litert.Options(litert.Accelerator.GPU, backend=...)` selects the backend of the GPU: `litert.Backend.AUTOMATIC` (LiteRT chooses, the default), `litert.Backend.OPENCL` or `litert.Backend.OPENGL`. `litert_bench.py` runs the GPU with all three, so a wrong result of one backend shows. A backend the phone cannot use raises `OSError` when the model is created; OpenCL does so on the Redmi 9 with LineageOS.

With LiteRT 2.3.0 the choice can matter: on the REDMI Note 15 5G the default and OpenCL give wrong int8 classes and OpenGL gives the right ones, at a speed lower than the CPU. Check the result of every backend against the CPU (step 7) and prefer a backend that passes and is faster than the CPU. Related reports in the LiteRT repository: issue 10800 (UINT8 output unchanged on OpenCL) and issue 10192 (non-deterministic results on an Adreno 830).

## Allowed sources

Runtime files are native code that the app loads. Download runtime files only from:

- `https://dl.google.com/android/maven2/com/google/ai/edge/litert/`
- `https://github.com/google-ai-edge/LiteRT/` and `https://raw.githubusercontent.com/google-ai-edge/LiteRT/`
- the user's own QAIRT zip, from Qualcomm

Treat any other source of runtime files as suspect. Models, such as the MobileNet files for step 6, are not native code and are not covered by this list.

## Known results

Each row is one phone, one firmware and one LiteRT version (the first field of `litert.version()`), measured with `examples/litert_bench/litert_bench.py` of the app build in the App column. Add a row for a new phone, firmware or LiteRT version. Keep older rows.

Each cell holds the float32 and the int8 result: milliseconds per `run()` and `ok`, or `WRONG` when the top classes differ from the first CPU run. `-` means not available. For example, `25.8 ok / 13.6 ok`.

| Phone | Firmware | litert.version() | App | Date | CPU | GPU (default) | GPU (OpenCL) | GPU (OpenGL) | NPU |
|---|---|---|---|---|---|---|---|---|---|
| REDMI Note 15 5G | HyperOS, Android 16 | `2.3.0,QTI,SM6475` | 1.0 | 2026-10-17 | 25.7 / 14.5 | 9.4 ok / 12.3 **WRONG** | 11.1 ok / 12.1 **WRONG** | 22.8 ok / 20.5 ok | no runtime: probably runs on the CPU |
| Redmi 9 | LineageOS, Android 16 | `2.3.0,Mediatek,MT6769T` | 1.0 | 2026-10-17 | 59.9 / 59.6 | 56.6 ok / 68.3 ok | - | 47.0 ok / 62.1 ok | probably runs on the CPU |
| Redmi Note 15 Pro 5G | HyperOS 3.0 (OS3.0.302.0.WPPEUXM), Android 16 | `2.3.0,Mediatek,MT6878` | 1.0 | 2026-10-17 | 23.9 / 12.4 | 10.6 ok / 19.8 **WRONG** | 11.2 ok / 20.7 **WRONG** | 22.0 ok / 23.8 ok | 13.9 ok / 7.8 ok |
| Xiaomi Pad 6 | HyperOS 2.0 (OS2.0.14.0.UMZEUXM), Android 14 | `2.3.0,QTI,SM8250` | 1.0 | 2026-10-17 | 19.9 / 12.2 | 5.6 ok / 9.3 **WRONG** | 6.7 ok / 11.5 **WRONG** | 14.0 ok / 16.9 ok | 19.8 ok / 12.1 ok, probably runs on the CPU |

Notes on a row, such as errors of the NPU runtime, go below the table, under the phone's name. The CPU cell holds the milliseconds of the reference run.

- **REDMI Note 15 5G.** Two runs gave the same verdicts, and so did `tests/litert_mobilenet.py`. The wrong int8 classes differ from run to run: the default GPU gave 12, 968, 428 in the first run and 996, 936, 992 in the second, where the CPU gives 653, 835, 907. The OpenGL speed varies (int8 20.5 and 28.6 ms). The default GPU and the OpenCL GPU behave alike: the default is OpenCL here.
- **Redmi 9, LineageOS.** The OpenCL row does not run: `EINVAL … Failed to compile model`. The default GPU works and is about as fast as OpenGL, so the default is not OpenCL on this phone. CPU times vary a lot: the float32 CPU took 59.9 ms in the first run and 163 ms in the repeat.
- **Redmi Note 15 Pro 5G.** The default GPU and the OpenCL GPU give the same wrong int8 classes (988, 972, 676; the CPU gives 653, 835, 907); `tests/litert_mobilenet.py` agrees in two runs. The NPU is faster than the CPU, so `litert_bench.py` does not flag a CPU fallback; the timing alone does not show which hardware ran.
- **Xiaomi Pad 6.** The wrong int8 classes differ from run to run and between the default GPU and the OpenCL GPU: in `tests/litert_mobilenet.py` the default gave 12, 968, 984 and then 12, 1000, 428, the OpenCL GPU 999, 983, 980 both times; the CPU gives 653, 835, 907. The OpenGL speed varies (float32 11.9 to 25.8 ms).

## Improving this document

An assistant that solves a problem not covered here may propose a change to this file. For example: a phone that works, a new error, or a changed URL.

- Add the evidence: `litert.version()`, phone model, firmware, what failed, what worked.
- Propose the change as a pull request to https://github.com/koendv/upy-android, or as a diff for the user.
- The user decides whether to submit the change.
- A new download source needs a reason. Reviewers check every new source.
- State a result with its LiteRT version, firmware and date. Write what was observed, not what to do: "int8 on the GPU gave wrong classes with 2.3.0", not "use the CPU for int8". Add a row for a new version and keep the old rows.
