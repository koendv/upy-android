// ulab_config.h, micropython android port.
// in CMakeLists.txt set ULAB_CONFIG_FILE="ulab_config.h"
// openmv flash budget trims for mcu's dropped.

#ifndef __ULAB_CONFIG_H__
#define __ULAB_CONFIG_H__
// support 3d/4d ndarrays: stacked image tensors, batched data
#define ULAB_MAX_DIMS (4)
#endif //__ULAB_CONFIG_H__
