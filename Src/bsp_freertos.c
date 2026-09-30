#include "bsp_gpio.h"
#include "bsp_time.h"
#include "stm32f103xb.h"
#include "FreeRTOS.h"
#include "task.h"

static StaticTask_t s_idle_task_tcb; // FreeRTOS 空闲任务使用的静态任务控制块。
static StackType_t s_idle_task_stack[configMINIMAL_STACK_SIZE]; // FreeRTOS 空闲任务使用的静态栈空间。
static StaticTask_t s_timer_task_tcb; // FreeRTOS 软件定时器服务任务的静态任务控制块。
static StackType_t s_timer_task_stack[configTIMER_TASK_STACK_DEPTH]; // 软件定时器服务任务的静态栈空间。

/**
 * @brief 向 FreeRTOS 提供空闲任务的静态内存。
 * @param tcb 接收任务控制块指针的地址。
 * @param stack 接收任务栈指针的地址。
 * @param depth 接收以栈字为单位的容量。
 */
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack,
                                   configSTACK_DEPTH_TYPE *depth)
{
    *tcb = &s_idle_task_tcb;
    *stack = s_idle_task_stack;
    *depth = configMINIMAL_STACK_SIZE;
}

/**
 * @brief 向 FreeRTOS 提供定时器服务任务的静态内存。
 * @param tcb 接收任务控制块指针的地址。
 * @param stack 接收任务栈指针的地址。
 * @param depth 接收以栈字为单位的容量。
 */
void vApplicationGetTimerTaskMemory(StaticTask_t **tcb, StackType_t **stack,
                                    configSTACK_DEPTH_TYPE *depth)
{
    *tcb = &s_timer_task_tcb;
    *stack = s_timer_task_stack;
    *depth = configTIMER_TASK_STACK_DEPTH;
}

/**
 * @brief 在 FreeRTOS 的每次 1 ms Tick 中推进板级毫秒计数。
 */
void vApplicationTickHook(void)
{
    BSP_Time_OnSysTick();
}

/**
 * @brief 在内核断言失败后保持电源灯亮，并用 WARNING 熄灭报告故障。
 */
void App_AssertFailed(void)
{
    BSP_Gpio_IndicateFault();
    __disable_irq();
    for (;;) { }
}

/**
 * @brief 在 FreeRTOS 动态内存申请失败时进入安全故障状态。
 */
void vApplicationMallocFailedHook(void)
{
    App_AssertFailed();
}

/**
 * @brief 在任务栈溢出时进入安全故障状态。
 * @param task 发生栈溢出的任务句柄。
 * @param name 发生栈溢出的任务名称。
 */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    App_AssertFailed();
}
