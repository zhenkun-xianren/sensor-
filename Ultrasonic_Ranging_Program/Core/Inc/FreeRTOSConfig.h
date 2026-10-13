#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include <stdint.h>
#include "bsp_board.h"
#include "bsp_time.h"
#include "bsp_backtrace.h"

#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configCPU_CLOCK_HZ BSP_BOARD_HCLK_HZ
#define configTICK_RATE_HZ 1000U
#define configMAX_PRIORITIES 4U
#define configMINIMAL_STACK_SIZE 128U
#define configMAX_TASK_NAME_LEN 16U
#define configRECORD_STACK_HIGH_ADDRESS 1
/* 仅在内核正常选中任务时读取元数据；故障路径不访问TCB或查询内核。
 * 当前ARM_CM0向低地址生长，pxEndOfStack是最高可用对齐栈字，转换为右开边界。 */
#define traceTASK_SWITCHED_IN() BSP_Backtrace_TaskSnapshot((uint32_t)pxCurrentTCB->pxStack, \
    (uint32_t)pxCurrentTCB->pxEndOfStack + sizeof(StackType_t), pxCurrentTCB->pcTaskName)
#if (configMAX_TASK_NAME_LEN != BSP_BACKTRACE_NAME_CAPACITY)
#error BACKTRACE_TASK_NAME_CAPACITY_MUST_MATCH_FREERTOS_CONFIG
#endif
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD 1
#define configUSE_TICKLESS_IDLE 0
#define configUSE_TASK_NOTIFICATIONS 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1U
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configKERNEL_PROVIDED_STATIC_MEMORY 0
#define configTOTAL_HEAP_SIZE (4U * 1024U)
#define configUSE_MALLOC_FAILED_HOOK 1
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MUTEXES 0
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_QUEUE_SETS 0
#define configQUEUE_REGISTRY_SIZE 0U
#define configUSE_CO_ROUTINES 0
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY 2U
#define configTIMER_QUEUE_LENGTH 8U
#define configTIMER_TASK_STACK_DEPTH 256U
#define configUSE_TRACE_FACILITY 0
#define configGENERATE_RUN_TIME_STATS 0
#define configUSE_NEWLIB_REENTRANT 0
#define configENABLE_MPU 0
#define configENABLE_FPU 0
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1

/* RVDS/ARM_CM0 用 PRIMASK 管理临界区，无 Cortex-M3/M4 的 BASEPRI 优先级阈值。
 * PendSV直接连接移植层汇编入口；SysTick由独立BSP处理函数转交，SVC在该移植层不使用。 */
#define xPortPendSVHandler PendSV_Handler

#if (configTICK_RATE_HZ != BSP_TIME_TICK_HZ)
#error BSP_TICK_AND_RTOS_TICK_MUST_BOTH_BE_ONE_MILLISECOND
#endif
#if (configUSE_TICKLESS_IDLE != 0)
#error BSP_MILLISECOND_TIMEBASE_REQUIRES_TICKLESS_IDLE_DISABLED
#endif

/**
 * @brief 记录 RTOS 断言并进入受控停止状态。
 * @param file 断言所在的静态源文件名。
 * @param line 断言所在源代码行号。
 */
void BSP_RtosAssert(const char *file, uint32_t line);

#define configASSERT(condition) do { if (!(condition)) { BSP_RtosAssert(__FILE__, (uint32_t)__LINE__); } } while (0)

#endif
