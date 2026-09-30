#include "app_system.h"
#include "FreeRTOS.h"
#include "task.h"

#define APP_SYSTEM_TASK_STACK_WORDS   192U
#define APP_SYSTEM_TASK_PRIORITY      (tskIDLE_PRIORITY + 1U)
#define APP_SYSTEM_HEARTBEAT_MS       1000U

volatile uint32_t g_app_system_heartbeat; // 每次系统任务执行后递增，用于确认调度器持续运行。
static StaticTask_t s_system_task_tcb; // 系统心跳任务使用的静态任务控制块。
static StackType_t s_system_task_stack[APP_SYSTEM_TASK_STACK_WORDS]; // 系统心跳任务的静态栈空间。

/**
 * @brief 每秒更新一次调度心跳，不改变两灯及安全输出。
 * @param context 保留的任务上下文参数，本任务不使用。
 */
static void app_system_task(void *context)
{
    TickType_t wake_tick = xTaskGetTickCount(); // 上一次周期唤醒的内核 Tick。

    (void)context;
    for (;;) {
        ++g_app_system_heartbeat;
        vTaskDelayUntil(&wake_tick, pdMS_TO_TICKS(APP_SYSTEM_HEARTBEAT_MS));
    }
}

/**
 * @brief 创建不操作现场外设的系统心跳任务。
 * @return 静态任务创建成功返回 1，失败返回 0。
 */
uint8_t App_System_Start(void)
{
    TaskHandle_t task; // 新建系统心跳任务的句柄，用于核对创建结果。

    g_app_system_heartbeat = 0U;
    task = xTaskCreateStatic(app_system_task, "System", APP_SYSTEM_TASK_STACK_WORDS,
                             NULL, APP_SYSTEM_TASK_PRIORITY, s_system_task_stack,
                             &s_system_task_tcb);
    return task != NULL ? 1U : 0U;
}
