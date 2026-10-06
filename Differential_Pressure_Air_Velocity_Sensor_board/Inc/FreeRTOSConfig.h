#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include "bsp_board.h"
#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configCPU_CLOCK_HZ BSP_BOARD_HCLK_HZ
#define configTICK_RATE_HZ BSP_BOARD_TICK_HZ
#define configMAX_PRIORITIES 5U
#define configMINIMAL_STACK_SIZE 128U
#define configMAX_TASK_NAME_LEN 16U
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configTOTAL_HEAP_SIZE 2048U
#define configUSE_MUTEXES 0
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_TASK_NOTIFICATIONS 1
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY 3U
#define configTIMER_QUEUE_LENGTH 8U
#define configTIMER_TASK_STACK_DEPTH 256U
#define configUSE_TICKLESS_IDLE 0
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_MALLOC_FAILED_HOOK 1
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_TRACE_FACILITY 1
#define configRECORD_STACK_HIGH_ADDRESS 1 /* 为 CmBacktrace 的公开 TaskStatus_t 记录栈上界。 */
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#define configGENERATE_RUN_TIME_STATS 0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configPRIO_BITS BSP_BOARD_NVIC_PRIORITY_BITS
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1 /* 供故障诊断获取当前任务，不访问私有 TCB。 */
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_vTaskSuspend 0
#define INCLUDE_vTaskDelete 0
#define configASSERT(condition) do { if (!(condition)) BSP_Board_Fault(BSP_FAULT_ASSERT); } while (0)
/* 仅SVC/PendSV映射到上游移植层，SysTick由BSP包装并维护独立毫秒计数。 */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#endif
