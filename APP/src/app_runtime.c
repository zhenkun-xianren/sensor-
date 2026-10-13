#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "app_runtime.h"

#define APP_RUNTIME_STACK_WORDS 256U
#define APP_RUNTIME_PRIORITY (tskIDLE_PRIORITY + 1U)
#define APP_RUNTIME_PERIOD_MS 1000U

static StaticTask_t s_runtime_tcb; // 运行任务的静态控制块。
static StackType_t s_runtime_stack[APP_RUNTIME_STACK_WORDS]; // 运行任务栈，256 个 32 位字。
static StaticTimer_t s_runtime_timer_storage; // 软件定时器的静态控制块。
static TaskHandle_t s_runtime_task; // 接收定时器通知的运行任务句柄。
static TimerHandle_t s_runtime_timer; // 周期运行软件定时器句柄。
static volatile AppRuntimeSnapshot s_runtime_snapshot; // 调试器可观察的软件诊断数据。
static int s_runtime_started; // 记录模块是否完整启动，失败时不得启动调度器。

/**
 * @brief 软件定时器仅更新计数并通知任务，不阻塞、不分配内存。
 * @param timer 发起回调的运行定时器句柄。
 */
static void runtime_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    s_runtime_snapshot.timer_count++;
    xTaskNotifyGive(s_runtime_task);
}

/**
 * @brief 阻塞等待软件定时器通知，更新计数及内存诊断数据。
 * @param argument 保留任务参数，当前不使用。
 */
static void runtime_task(void *argument)
{
    uint32_t notification_count; // 保存本次等待期间积累的通知数量。
    (void)argument;
    for (;;) {
        notification_count = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        taskENTER_CRITICAL();
        s_runtime_snapshot.wake_count += notification_count;
        s_runtime_snapshot.task_stack_free_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        s_runtime_snapshot.heap_free_bytes = (uint32_t)xPortGetFreeHeapSize();
        taskEXIT_CRITICAL();
    }
}

/**
 * @brief 创建静态任务及定时器，将启动命令送入定时器服务队列。
 * @return 0 表示启动成功；非 0 表示已有对象、创建失败或启动命令入队失败。
 */
int AppRuntime_Start(void)
{
    const TickType_t period_ticks = pdMS_TO_TICKS(APP_RUNTIME_PERIOD_MS); // 软件定时器周期，以 Tick 为单位。
    if ((s_runtime_task != NULL) || (s_runtime_started != 0) || (period_ticks == 0U)) {
        return -1;
    }
    s_runtime_task = xTaskCreateStatic(runtime_task, "runtime", APP_RUNTIME_STACK_WORDS,
                                      NULL, APP_RUNTIME_PRIORITY, s_runtime_stack, &s_runtime_tcb);
    if (s_runtime_task == NULL) {
        return -1;
    }
    s_runtime_timer = xTimerCreateStatic("runtime_timer", period_ticks, pdTRUE,
                                         NULL, runtime_timer_callback, &s_runtime_timer_storage);
    if (s_runtime_timer == NULL) {
        return -1;
    }
    /* 调度器尚未启动，只允许零等待入队；失败交由总启动流程进入受控错误状态。 */
    if (xTimerStart(s_runtime_timer, 0U) != pdPASS) {
        return -1;
    }
    s_runtime_started = 1;
    return 0;
}

/**
 * @brief 在临界区复制一致的软件诊断快照，仅供任务上下文使用。
 * @param output 保存诊断快照的非空对象。
 * @return 0 表示成功，非 0 表示输出为空或模块未完整启动。
 */
int AppRuntime_GetSnapshot(AppRuntimeSnapshot *output)
{
    if ((output == NULL) || (s_runtime_started == 0)) {
        return -1;
    }
    taskENTER_CRITICAL();
    output->wake_count = s_runtime_snapshot.wake_count;
    output->timer_count = s_runtime_snapshot.timer_count;
    output->task_stack_free_words = s_runtime_snapshot.task_stack_free_words;
    output->heap_free_bytes = s_runtime_snapshot.heap_free_bytes;
    taskEXIT_CRITICAL();
    return 0;
}
