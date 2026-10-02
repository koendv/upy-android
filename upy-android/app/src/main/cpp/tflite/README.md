# TensorFlow Lite C API headers

`include/` holds the classic TensorFlow Lite C API headers, committed to
this repo. Same deliberate exception as `litert/include/` -- see that
directory's own README for the general rationale.

## Why these headers, from this source

`tflite_module.cpp` links `libLiteRt.so` -- the SAME shared library the
`litert` module links, not a second TFLite build. `libLiteRt.so` already
exports the classic `TfLite*` C API alongside LiteRT's own `LiteRt*` API,
since LiteRT is built from its own copy of the TFLite source. One copy
of TFLite in the process, not two racing the dynamic linker's symbol
resolution (confirmed via `nm -D`: `libLiteRt.so` exports 207 `TfLite*`
symbols before this module existed).

That means the headers must come from the exact same source LiteRT's
`libLiteRt.so` was built from -- LiteRT's own repo, tag
`v<litert.version>` (the same tag `update-litert-headers.sh` uses) --
not from upstream `tensorflow/tensorflow`, whose `tensorflow/lite/c/...`
include paths have since been renamed to `tflite/...` in LiteRT's tree
and may not even match ABI-wise.

## What is here

| Path | Source |
|---|---|
| `include/tflite/**/*.h` | LiteRT source, tag `v<litert.version>`, the real `#include` closure of `tflite/c/c_api.h` |
| `include/LITERT_VERSION` | the LiteRT version these headers belong to (same version as `litert/include/LITERT_VERSION`) |

No `lib/`: there is no separate `libtensorflowlite_*.so` for this
module. `tflite_module.cpp` links `litert/lib/libLiteRt.so` (see
`CMakeLists.txt`'s comment on the `litert` imported target).

The header set was determined by actually preprocessing
`tflite/c/c_api.h` (`clang -E -M`), not by guessing from a directory
listing -- re-run that check on every version bump, since a new LiteRT
release could add a transitive header.

License: Apache License 2.0, see NOTICE.html.

## Upgrading

Headers move in lockstep with `litert.version` (`upstream.properties`)
-- there is no independent `tflite.version`. After bumping
`litert.version` and running `update-litert-headers.sh` as usual, also
run:

```
./native-bringup/update-tflite-headers.sh
```

Review the diff and commit it alongside the `litert/include/` diff.
