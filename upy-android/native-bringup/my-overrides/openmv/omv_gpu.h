// upy-android OpenMV support layer. Stub for OpenMV's common/omv_gpu.h
// (their GPU-blit driver header). imlib.c includes it unconditionally
// but only calls into it when OMV_GPU_ENABLE == 1, which is never
// defined here. No function bodies needed, just enough to satisfy the
// #include.
#ifndef UPY_ANDROID_STUB_OMV_GPU_H
#define UPY_ANDROID_STUB_OMV_GPU_H

#endif
