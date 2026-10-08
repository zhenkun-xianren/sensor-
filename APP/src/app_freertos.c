#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <stddef.h>

static StaticTask_t s_idle_tcb; // 空闲任务静态控制块。
static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE]; // 空闲任务栈，单位为StackType_t字。
static StaticTask_t s_timer_tcb; // 软件定时器服务任务静态控制块。
static StackType_t s_timer_stack[configTIMER_TASK_STACK_DEPTH]; // 定时器任务栈，单位为StackType_t字。
static const char * volatile s_assert_file; // 保存最后一次断言来源路径。
static volatile uint32_t s_assert_line; // 保存最后一次断言行号。
static volatile TaskHandle_t s_overflow_task; // 保存检测到栈溢出的任务。

/**
 * @brief 为FreeRTOS空闲任务提供静态内存。
 * @param ppxIdleTaskTCBBuffer 接收控制块地址。
 * @param ppxIdleTaskStackBuffer 接收栈地址。
 * @param puxIdleTaskStackSize 接收栈容量，单位为StackType_t字。
 */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
    StackType_t **ppxIdleTaskStackBuffer, configSTACK_DEPTH_TYPE *puxIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &s_idle_tcb;
    *ppxIdleTaskStackBuffer = s_idle_stack;
    *puxIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/**
 * @brief 为FreeRTOS软件定时器服务任务提供静态内存。
 * @param ppxTimerTaskTCBBuffer 接收控制块地址。
 * @param ppxTimerTaskStackBuffer 接收栈地址。
 * @param puxTimerTaskStackSize 接收栈容量，单位为StackType_t字。
 */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
    StackType_t **ppxTimerTaskStackBuffer, configSTACK_DEPTH_TYPE *puxTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer = &s_timer_tcb;
    *ppxTimerTaskStackBuffer = s_timer_stack;
    *puxTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

/**
 * @brief 保存断言信息并停在故障处理处。
 * @param file 触发断言的源码路径。
 * @param line 触发断言的行号。
 */
void App_RTOS_Assert(const char *file, uint32_t line)
{
    s_assert_file = file;
    s_assert_line = line;
    taskDISABLE_INTERRUPTS();
    for (;;) { }
}

/**
 * @brief 保存栈溢出任务信息并停止依赖不可靠栈的后续运行。
 * @param task 栈溢出任务句柄。
 * @param name 栈溢出任务名称，由内核提供。
 */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)name;
    s_overflow_task = task;
    taskDISABLE_INTERRUPTS();
    for (;;) { }
}
