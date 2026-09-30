#include "bsp_compiler.h"

/* 每个 Target 只展开一份与实际编译器对应的 FreeRTOS Cortex-M3 移植实现。 */
#if (FREERTOS_ARM_COMPILER == 5)
#include "portable/RVDS/ARM_CM3/port.c"
#elif (FREERTOS_ARM_COMPILER == 6)
#include "portable/GCC/ARM_CM3/port.c"
#else
#error "Unsupported FREERTOS_ARM_COMPILER"
#endif
