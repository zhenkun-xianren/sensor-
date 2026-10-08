#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
#include "bsp_board.h"

/* 只使用静态任务、队列和软件定时器；单核Cortex-M3，Tick为1ms。 */
#define configUSE_PREEMPTION                  1
#define configUSE_TIME_SLICING                1
#define configCPU_CLOCK_HZ                    BSP_BOARD_HCLK_HZ
#define configTICK_RATE_HZ                    BSP_BOARD_TICK_HZ
#define configTICK_TYPE_WIDTH_IN_BITS         TICK_TYPE_WIDTH_32_BITS
#define configMAX_PRIORITIES                  5U
#define configMINIMAL_STACK_SIZE              128U
#define configMAX_TASK_NAME_LEN               16U
#define configSUPPORT_STATIC_ALLOCATION       1
#define configSUPPORT_DYNAMIC_ALLOCATION      1
/* 唯一heap_4实现随工程保留；本次任务、队列和定时器全部静态创建，不调用动态分配API。 */
#define configTOTAL_HEAP_SIZE                 2048U
#define configUSE_TIMERS                      1
#define configTIMER_TASK_PRIORITY             3U
#define configTIMER_QUEUE_LENGTH              8U
#define configTIMER_TASK_STACK_DEPTH          256U
#define configUSE_IDLE_HOOK                   0
#define configUSE_TICK_HOOK                   0
#define configUSE_MUTEXES                     1
#define configUSE_RECURSIVE_MUTEXES           0
#define configUSE_COUNTING_SEMAPHORES         0
#define configUSE_TASK_NOTIFICATIONS          1
#define configUSE_TRACE_FACILITY              0
#define configCHECK_FOR_STACK_OVERFLOW        2
#define configUSE_MALLOC_FAILED_HOOK          0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
/* RVDS移植层将这些宏直接用于ARMCC内嵌汇编，数值不得带C语言U后缀。 */
#define configPRIO_BITS                       4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY       (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY  (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define INCLUDE_xTaskGetSchedulerState        1
#define INCLUDE_vTaskDelay                    1
#define INCLUDE_uxTaskGetStackHighWaterMark    1

/* 正式向量由唯一的FreeRTOS移植层提供；生成参考空处理器在USER CODE内改名。 */
#define vPortSVCHandler                       SVC_Handler
#define xPortPendSVHandler                    PendSV_Handler

/**
 * @brief 记录RTOS断言并停在可定位的故障处。
 * @param file 触发断言的源码路径。
 * @param line 触发断言的行号。
 */
void App_RTOS_Assert(const char *file, uint32_t line);
#define configASSERT(condition) do { if (!(condition)) App_RTOS_Assert(__FILE__, __LINE__); } while (0)

#endif
