# LiteRT C API headers

`include/` holds LiteRT's C API headers, committed to this repo. This is a
deliberate exception to the rule that third-party code is fetched at build
time, not committed.

## Why committed

- `litert_module.cpp` calls the LiteRT C API for tensor inspection:
  `CompiledModel.get_input/output_tensor_type()` and
  `get_input/output_tensor_quantization()` (used by `libraries/ml`).
- The Kotlin `litert-api` has no quantization API, and its tensor types
  cover only INT, FLOAT, INT8, BOOLEAN and INT64 (checked on 2.2.0).
- Neither the `litert` nor the `litert-api` AAR ships C headers.
- The headers are small text files that only change with a LiteRT release.
  Committing them keeps the build down to the AARs, with no LiteRT source
  checkout.

## What is here

| Path | Source |
|---|---|
| `include/litert/c/*.h` | LiteRT source, tag `v<litert.version>`, `litert/c/` (top level only) |
| `include/litert/build_common/build_config.h` | stand-in, written by the update script (see below) |
| `include/LITERT_VERSION` | the LiteRT version these headers belong to |
| `lib/` | not committed: `libLiteRt.so` and `libLiteRtClGlAccelerator.so`, extracted from the `litert` AAR on every build (`extractLitert` in `app/build.gradle.kts`) |

- Only `litert/c/` is needed. `litert_module.cpp` includes
  `litert/c/{litert_common,litert_environment,litert_model,litert_model_types}.h`,
  and their includes stay inside `litert/c/` plus `build_config.h`.
- LiteRT's `tflite/` and `litert/c/internal/` headers are not used and not committed.
- `build_config.h` is not a file in the LiteRT repo. Upstream generates it at
  build time (Bazel genrule, `litert/build_common/BUILD`, rule
  `build_config_header`), picking one of several variants. The default variant
  is empty, and the stand-in matches that. Recheck this on every upgrade.
- License: Apache License 2.0, see NOTICE.html.

## Upgrading LiteRT

1. Set `litert.version` in `upy-android/upstream.properties`.
2. Run `native-bringup/update-litert-headers.sh`.
3. Review the header diff. Check `litert/build_common/BUILD` upstream: is the
   default `build_config.h` variant still empty?
4. Regenerate the dependency lock file: `./gradlew :app:dependencies --write-locks`.
5. Build, then run the `litert` and `ml` selftests on a device.
6. Commit `upstream.properties`, `include/`, and `app/gradle.lockfile` together.

The build refuses to run when `include/LITERT_VERSION` does not match
`litert.version`, so step 2 cannot be forgotten.
