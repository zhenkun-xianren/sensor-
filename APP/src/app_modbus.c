#include "app_modbus.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#define APP_MODBUS_TASK_STACK_WORDS   128U
#define APP_MODBUS_TASK_PRIORITY      (tskIDLE_PRIORITY + 1U)
#define APP_MODBUS_PERIOD_MS          1000U

volatile uint32_t g_app_modbus_task_heartbeat; // Modbus APP 周期任务的调度计数。
volatile uint32_t g_app_modbus_timer_count; // Modbus APP 软件定时器回调计数。
static StaticTask_t s_modbus_task_tcb; // Modbus APP 周期任务的静态控制块。
static StackType_t s_modbus_task_stack[APP_MODBUS_TASK_STACK_WORDS]; // Modbus APP 周期任务的静态栈。
static StaticTimer_t s_modbus_timer_storage; // Modbus APP 周期软件定时器的静态存储。
static TimerHandle_t s_modbus_timer; // Modbus APP 周期软件定时器句柄。

/**
 * @brief 每秒递增 Modbus APP 调试心跳，不发送任何串口数据。
 * @param context 保留的任务上下文参数，本任务不使用。
 */
static void app_modbus_task(void *context)
{
    TickType_t wake_tick = xTaskGetTickCount(); // 周期任务上一次唤醒的内核 Tick。

    (void)context;
    for (;;) {
        ++g_app_modbus_task_heartbeat;
        vTaskDelayUntil(&wake_tick, pdMS_TO_TICKS(APP_MODBUS_PERIOD_MS));
    }
}

/**
 * @brief 每次软件定时器到期时递增调试计数，不发送任何串口数据。
 * @param timer 到期的软件定时器句柄，本回调不读取其上下文。
 */
static void app_modbus_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    ++g_app_modbus_timer_count;
}

/**
 * @brief 创建 Modbus APP 静态周期任务和软件定时器；本阶段不发送数据。
 * @return 任务、定时器创建并成功启动返回 1，否则返回 0。
 */
uint8_t App_Modbus_Start(void)
{
    TaskHandle_t task; // 新建 Modbus APP 周期任务的句柄。

    g_app_modbus_task_heartbeat = 0U;
    g_app_modbus_timer_count = 0U;
    s_modbus_timer = NULL;

    task = xTaskCreateStatic(app_modbus_task, "Modbus", APP_MODBUS_TASK_STACK_WORDS,
                             NULL, APP_MODBUS_TASK_PRIORITY, s_modbus_task_stack,
                             &s_modbus_task_tcb);
    if (task == NULL) {
        return 0U;
    }

    s_modbus_timer = xTimerCreateStatic("ModbusDiag",
                                        pdMS_TO_TICKS(APP_MODBUS_PERIOD_MS),
                                        pdTRUE,
                                        NULL,
                                        app_modbus_timer_callback,
                                        &s_modbus_timer_storage);
    if (s_modbus_timer == NULL || xTimerStart(s_modbus_timer, 0U) != pdPASS) {
        return 0U;
    }
    return 1U;
}
