#ifndef BSP_COMPILER_H
#define BSP_COMPILER_H

/* Keil Target 同时选择实际编译器、宏定义及后续 FreeRTOS 移植层。 */
#if !defined(FREERTOS_ARM_COMPILER)
#error "Keil target must define FREERTOS_ARM_COMPILER as 5 or 6"
#elif (FREERTOS_ARM_COMPILER == 5)
#if !defined(__CC_ARM) || defined(__clang__)
#error "FREERTOS_ARM_COMPILER=5 requires ARM Compiler 5"
#endif
#elif (FREERTOS_ARM_COMPILER == 6)
#if !defined(__clang__) || !defined(__ARMCC_VERSION)
#error "FREERTOS_ARM_COMPILER=6 requires ARM Compiler 6"
#endif
#else
#error "FREERTOS_ARM_COMPILER must be 5 or 6"
#endif

#endif /* BSP_COMPILER_H */
