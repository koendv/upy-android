// qstr-scan-only stub.
// See micropython_embed.mk's qstr-stub CFLAGS comment.
// Deliberately empty, no real declarations.
// The host gcc qstr scan only preprocesses (-E),
// never type-checks camera_module.cpp's actual NDK Camera2 API usage,
// so this only needs to satisfy the #include line, not provide real symbols.
// Not used by the real build.
// The Android NDK clang wrapper resolves these to the genuine NDK sysroot headers instead.

#ifndef UPY_ANDROID_QSTR_STUB_CAMERA_NDKCAMERAMANAGER_H
#define UPY_ANDROID_QSTR_STUB_CAMERA_NDKCAMERAMANAGER_H
#endif
