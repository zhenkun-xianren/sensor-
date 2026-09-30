#include "app_main.h"
#include "app_config.h"
#include "app_buzzer.h"
#include "app_display.h"
#include "app_led.h"
#include "app_ir_relay.h"
#include "app_modbus.h"
#include "FreeRTOS.h"
#include "task.h"

static StaticTask_t s_idle_tcb;
static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE];
static StaticTask_t s_timer_task_tcb;
static StackType_t s_timer_task_stack[APP_TIMER_SERVICE_TASK_STACK_WORDS];

/**
 * @brief 按全局启动流程启动 APP 功能模块。
 * @return 所有功能模块启动成功返回 1；否则返回 0。
 */
uint8_t App_Init(void)
{
    if (AppBuzzer_Init() == 0U) {
        return 0U;
    }
    if (AppDisplay_Init() == 0U) {
        return 0U;
    }
    if (AppLed_Init() == 0U) {
        return 0U;
    }
    if (AppIrRelay_Init() == 0U) {
        return 0U;
    }
    return AppModbus_Init();
}

/**
 * @brief 向 FreeRTOS 提供空闲任务所需的静态内存。
 * @param tcb 接收空闲任务控制块指针的地址。
 * @param stack 接收空闲任务栈指针的地址。
 * @param depth 接收以栈字为单位的任务栈深度。
 */
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack,
                                   configSTACK_DEPTH_TYPE *depth)
{
    *tcb = &s_idle_tcb;
    *stack = s_idle_stack;
    *depth = configMINIMAL_STACK_SIZE;
}

/**
 * @brief 向 FreeRTOS 提供软件定时器服务任务所需的静态内存。
 * @param tcb 接收软件定时器服务任务控制块指针的地址。
 * @param stack 接收软件定时器服务任务栈指针的地址。
 * @param depth 接收以栈字为单位的任务栈深度。
 */
void vApplicationGetTimerTaskMemory(StaticTask_t **tcb, StackType_t **stack,
                                    configSTACK_DEPTH_TYPE *depth)
{
    *tcb = &s_timer_task_tcb;
    *stack = s_timer_task_stack;
    *depth = APP_TIMER_SERVICE_TASK_STACK_WORDS;
}

/**
 * @brief 断言发生不可恢复的错误后停止应用运行。
 */
void App_AssertFailed(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) { }
}

/**
 * @brief 将 FreeRTOS 内存分配失败交由应用故障处理函数处理。
 */
void vApplicationMallocFailedHook(void)
{
    App_AssertFailed();
}

/**
 * @brief 将 FreeRTOS 任务栈溢出交由应用故障处理函数处理。
 * @param task 发生栈溢出的任务句柄。
 * @param name 发生栈溢出的任务名称。
 */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    App_AssertFailed();
}
