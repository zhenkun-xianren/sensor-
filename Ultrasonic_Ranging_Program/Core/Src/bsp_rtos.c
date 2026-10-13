#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "bsp_rtos.h"
#include "bsp_fault.h"

static StaticTask_t s_idle_tcb; // Idle 任务静态控制块。
static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE]; // Idle 任务栈，单位为 32 位字。
static StaticTask_t s_timer_tcb; // 软件定时器服务任务静态控制块。
static StackType_t s_timer_stack[configTIMER_TASK_STACK_DEPTH]; // 定时器服务任务栈，单位为 32 位字。

/** @brief 移植层提供的内核Tick实现，由唯一手写SysTick处理函数间接调用。 */
extern void xPortSysTickHandler(void);

/** @brief 调度器启动后推进内核Tick；独立BSP毫秒计时由手写SysTick先维护。 */
void BSP_RtosTick(void)
{
    /* 调度器挂起仍须处理 Tick，以记录待补偿节拍；仅启动前跳过内核。 */
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

/**
 * @brief 为 Idle 任务提供静态控制块和栈。
 * @param ppxIdleTaskTCBBuffer 接收静态控制块地址的非空输出指针。
 * @param ppxIdleTaskStackBuffer 接收静态栈地址的非空输出指针。
 * @param puxIdleTaskStackSize 接收栈元素数量的非空输出指针，单位为 32 位字。
 */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                  StackType_t **ppxIdleTaskStackBuffer,
                                  configSTACK_DEPTH_TYPE *puxIdleTaskStackSize)
{
    configASSERT(ppxIdleTaskTCBBuffer != NULL);
    configASSERT(ppxIdleTaskStackBuffer != NULL);
    configASSERT(puxIdleTaskStackSize != NULL);
    *ppxIdleTaskTCBBuffer = &s_idle_tcb;
    *ppxIdleTaskStackBuffer = s_idle_stack;
    *puxIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/**
 * @brief 为软件定时器服务任务提供静态控制块和栈。
 * @param ppxTimerTaskTCBBuffer 接收静态控制块地址的非空输出指针。
 * @param ppxTimerTaskStackBuffer 接收静态栈地址的非空输出指针。
 * @param puxTimerTaskStackSize 接收栈元素数量的非空输出指针，单位为 32 位字。
 */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                   StackType_t **ppxTimerTaskStackBuffer,
                                   configSTACK_DEPTH_TYPE *puxTimerTaskStackSize)
{
    configASSERT(ppxTimerTaskTCBBuffer != NULL);
    configASSERT(ppxTimerTaskStackBuffer != NULL);
    configASSERT(puxTimerTaskStackSize != NULL);
    *ppxTimerTaskTCBBuffer = &s_timer_tcb;
    *ppxTimerTaskStackBuffer = s_timer_stack;
    *puxTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

/**
 * @brief 记录 RTOS 断言位置并停止执行。
 * @param file 断言所在静态源文件名。
 * @param line 断言所在代码行号。
 */
void BSP_RtosAssert(const char *file, uint32_t line)
{
    BSP_Fault_Stop(BSP_FAULT_ASSERT, file, line);
}

/**
 * @brief 记录栈溢出并停止，避免在栈已损坏后继续执行。
 * @param task 发生栈溢出的任务句柄，当前仅由内核传入。
 * @param task_name 发生溢出的任务名，当前不访问其内容。
 */
void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    BSP_Fault_Stop(BSP_FAULT_STACK, __FILE__, (uint32_t)__LINE__);
}

/** @brief 内存分配失败时记录故障并停止，不继续使用空句柄。 */
void vApplicationMallocFailedHook(void)
{
    BSP_Fault_Stop(BSP_FAULT_ALLOC, __FILE__, (uint32_t)__LINE__);
}
