// upy-android OpenMV support layer -- stub for OpenMV's
// common/omv_gpu.h (their GPU-blit driver header). imlib.c includes it
// unconditionally but only calls into it when OMV_GPU_ENABLE == 1 (see
// imlib.c's own guard), which we never define -- no function bodies
// needed here, just enough to satisfy the #include.
#ifndef UPY_ANDROID_STUB_OMV_GPU_H
#define UPY_ANDROID_STUB_OMV_GPU_H

#endif
