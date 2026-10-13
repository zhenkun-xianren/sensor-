#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "app_ir_remote.h"

#define APP_IR_REMOTE_TASK_STACK_WORDS 256U
#define APP_IR_REMOTE_TASK_PRIORITY (tskIDLE_PRIORITY + 1U)
#define APP_IR_EVENT_POLL_PERIOD_MS 10U
#define APP_IR_REMOTE_MIN_PERIOD_TICKS 1U
#define APP_IR_REMOTE_TIMER_COMMAND_WAIT_TICKS 0U

static StaticTask_t s_event_task_storage; // 保存红外事件处理任务的静态控制块。
static StackType_t s_event_task_stack[APP_IR_REMOTE_TASK_STACK_WORDS]; // 保存红外事件处理任务栈。
static StaticTimer_t s_poll_timer_storage; // 保存红外轮询定时器的静态控制块。
static TaskHandle_t s_event_task; // 保存红外事件处理任务句柄。
static TimerHandle_t s_poll_timer; // 保存每十毫秒触发的红外轮询定时器句柄。
static DevIrRemote *s_ir_remote; // 保存已初始化的红外接收设备接口。
static AppIrRemoteEventHandler s_event_handler; // 保存独立业务模块提供的事件回调。
static AppIrRemoteReadyHandler s_ready_handler; // 保存独立业务模块提供的就绪查询回调。
static bool s_ir_fault = false; // 锁存红外设备事件读取故障。
static bool s_handler_fault = false; // 锁存业务事件处理故障，停止继续读取队列。
static bool s_module_started = false; // 标记红外静态任务及轮询定时器是否完整启动。

/**
 * @brief 周期通知事件任务检查红外队列，不在定时器服务任务中处理业务事件。
 * @param timer 触发本次轮询通知的周期定时器句柄。
 */
static void ir_poll_callback(TimerHandle_t timer)
{
    (void)timer;
    xTaskNotifyGive(s_event_task);
}

/**
 * @brief 等待轮询通知并排空红外队列，将事件交给已绑定的业务回调。
 * @param argument 保留任务参数，当前不使用。
 */
static void ir_event_task(void *argument)
{
    IrKeyEvent event; // 保存本次从红外设备读取的完整键事件。
    DevIrResult read_result; // 保存红外事件读取结果。
    (void)argument;

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_ir_fault || s_handler_fault || !s_ready_handler()) {
            continue;
        }
        for (;;) {
            read_result = s_ir_remote->read_event(s_ir_remote, &event);
            if (read_result == DEV_IR_NO_EVENT) {
                break;
            }
            if (read_result != DEV_IR_OK) {
                s_ir_fault = true;
                break;
            }
            if (!s_event_handler(&event)) {
                s_handler_fault = true;
                break;
            }
        }
    }
}

/**
 * @brief 初始化红外设备、绑定业务回调并创建静态任务及十毫秒软件定时器。
 * @param event_handler 非空业务事件回调，收到事件后在任务上下文调用。
 * @param ready_handler 非空业务就绪回调，每次轮询读取队列前调用。
 * @return 成功返回APP_IR_REMOTE_OK，重复启动、接口缺失或设备及资源失败返回APP_IR_REMOTE_FAILED。
 */
AppIrRemoteResult AppIrRemote_Start(AppIrRemoteEventHandler event_handler,
                                  AppIrRemoteReadyHandler ready_handler)
{
    const TickType_t poll_period = pdMS_TO_TICKS(APP_IR_EVENT_POLL_PERIOD_MS); // 红外队列检查间隔对应的Tick数。
    DevIrResult result; // 保存红外接收初始化结果。

    if (s_module_started || s_event_task != NULL ||
        poll_period < APP_IR_REMOTE_MIN_PERIOD_TICKS ||
        event_handler == NULL || ready_handler == NULL) {
        return APP_IR_REMOTE_FAILED;
    }
    s_ir_remote = GetIrRemote();
    if (s_ir_remote == NULL || s_ir_remote->init == NULL || s_ir_remote->read_event == NULL) {
        return APP_IR_REMOTE_FAILED;
    }
    result = s_ir_remote->init(s_ir_remote);
    if (result != DEV_IR_OK) {
        return APP_IR_REMOTE_FAILED;
    }
    s_event_handler = event_handler;
    s_ready_handler = ready_handler;
    s_event_task = xTaskCreateStatic(ir_event_task, "ir_remote", APP_IR_REMOTE_TASK_STACK_WORDS,
                                     NULL, APP_IR_REMOTE_TASK_PRIORITY,
                                     s_event_task_stack, &s_event_task_storage);
    if (s_event_task == NULL) {
        return APP_IR_REMOTE_FAILED;
    }
    s_poll_timer = xTimerCreateStatic("ir_poll", poll_period, pdTRUE, NULL,
                                      ir_poll_callback, &s_poll_timer_storage);
    if (s_poll_timer == NULL ||
        xTimerStart(s_poll_timer, APP_IR_REMOTE_TIMER_COMMAND_WAIT_TICKS) != pdPASS) {
        return APP_IR_REMOTE_FAILED;
    }
    s_module_started = true;
    return APP_IR_REMOTE_OK;
}
