// Own file, no upstream ulab equivalent -- same pattern OpenMV uses on
// every one of its own boards (boards/*/ulab_config.h), which all
// override ULAB_MAX_DIMS to 4 (ulab's own library default, in
// ulab.h, is 2). Matched here so OpenMV example scripts/examples that
// build 3D/4D ndarrays (stacked image tensors, batched data) behave
// the same on this port as on real OpenMV hardware. Wired in via
// CMakeLists.txt's ULAB_CONFIG_FILE="ulab_config.h" compile
// definition, same mechanism as CMSIS_MCU_H's cmsis_mcu_stub.h.
//
// Deliberately NOT copying OpenMV's other overrides (ULAB_SUPPORTS_
// COMPLEX=0, ULAB_SCIPY_HAS_OPTIMIZE_MODULE/SPECIAL_MODULE=0) --
// those are flash-budget trims for their MCU targets, not
// compatibility requirements; Android has no equivalent flash
// constraint, and turning capability OFF to match a constrained
// embedded target would be a regression, not parity.
#ifndef __ULAB_CONFIG_H__
#define __ULAB_CONFIG_H__
#define ULAB_MAX_DIMS (4)
#endif //__ULAB_CONFIG_H__
