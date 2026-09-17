// qstr-scan-only stub -- see micropython_embed.mk's qstr-stub CFLAGS
// comment. Deliberately empty (no real declarations): the host gcc qstr
// scan only preprocesses (-E), it never type-checks imu_module.cpp's
// actual NDK looper API usage, so this only needs to satisfy the
// #include line, not provide real symbols. NOT used by the real build
// (the Android NDK clang wrapper resolves this to the genuine NDK
// sysroot header instead).
#ifndef UPY_ANDROID_QSTR_STUB_ANDROID_LOOPER_H
#define UPY_ANDROID_QSTR_STUB_ANDROID_LOOPER_H
#endif
