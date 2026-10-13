#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "app_modbus.h"

#define APP_MODBUS_STACK_WORDS 256U
/* 发送路径沿用驱动库轮询TXE；最高任务优先级避免普通任务抢占造成RTU字节间隙。
 * 每帧8字节在9600/8N1约8.33ms；硬件中断保持开放，空闲时任务阻塞等待通知。 */
#define APP_MODBUS_TASK_PRIORITY (configMAX_PRIORITIES - 1U)

static StaticTask_t s_task_storage; // 保存发送任务静态控制块。
static StackType_t s_task_stack[APP_MODBUS_STACK_WORDS]; // 保存256个32位字的静态任务栈。
static StaticTimer_t s_timer_storage; // 保存周期软件定时器静态控制块。
static TaskHandle_t s_task; // 保存通知接收任务句柄。
static TimerHandle_t s_timer; // 保存每秒触发的软件定时器句柄。
static DevModbus *s_device; // 保存只发送Modbus设备对象。
static volatile AppModbusSnapshot s_snapshot; // 保存本地发送诊断。
static uint8_t s_device_ready; // 记录是否可发送，失败后下个独立周期重新初始化。
static uint8_t s_started; // 记录模块对象是否完整启动。

/**
 * @brief 软件定时器只发送任务通知，不在定时器服务任务中执行串口操作。
 * @param timer 触发本次回调的定时器句柄。
 */
static void modbus_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    xTaskNotifyGive(s_task);
}

/**
 * @brief 每次通知只发送一帧，不补发积压通知，不接收或重试本次失败帧。
 * @param argument 保留任务参数，当前不使用。
 */
static void modbus_task(void *argument)
{
    uint32_t notifications; // 保存等待期间积累的通知数。
    DevModbusStatus result; // 保存本次初始化或发送状态。
    (void)argument;
    for (;;) {
        notifications = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_device_ready == 0U) {
            /* 超时关闭UART后，下一次周期独立初始化；不在失败周期内自动重发。 */
            result = s_device->init(s_device);
            s_device_ready = result == DEV_MODBUS_OK ? 1U : 0U;
        } else {
            result = DEV_MODBUS_OK;
        }
        if (result == DEV_MODBUS_OK) {
            result = s_device->sendHoldingRegister(s_device, APP_MODBUS_SLAVE_ADDRESS,
                        APP_MODBUS_REGISTER_ADDRESS, APP_MODBUS_REGISTER_VALUE);
        }
        taskENTER_CRITICAL();
        s_snapshot.trigger_count += notifications;
        s_snapshot.last_status = result;
        if (result == DEV_MODBUS_OK) {
            s_snapshot.send_count++;
        } else {
            s_snapshot.failure_count++;
            s_device_ready = 0U;
        }
        s_snapshot.task_stack_free_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        taskEXIT_CRITICAL();
    }
}

/**
 * @brief 完成设备初始化及静态任务、定时器创建，创建失败交由全局启动处理。
 * @return 0表示启动成功，非0表示重复启动或任一初始化、创建操作失败。
 */
int AppModbus_Start(void)
{
    const TickType_t period = pdMS_TO_TICKS(APP_MODBUS_PERIOD_MS); // 保存周期对应的Tick数。
    DevModbusStatus result; // 保存设备初始化结果。
    if (s_started != 0U || s_task != NULL || period == 0U) {
        return -1;
    }
    s_device = GetModbus();
    if (s_device == NULL || s_device->init == NULL || s_device->sendHoldingRegister == NULL) {
        return -1;
    }
    result = s_device->init(s_device);
    s_snapshot.last_status = result;
    if (result != DEV_MODBUS_OK) {
        return -1;
    }
    s_device_ready = 1U;
    s_task = xTaskCreateStatic(modbus_task, "modbus_tx", APP_MODBUS_STACK_WORDS,
                              NULL, APP_MODBUS_TASK_PRIORITY, s_task_stack, &s_task_storage);
    if (s_task == NULL) {
        return -1;
    }
    s_timer = xTimerCreateStatic("modbus_1s", period, pdTRUE, NULL,
                                 modbus_timer_callback, &s_timer_storage);
    if (s_timer == NULL || xTimerStart(s_timer, 0U) != pdPASS) {
        return -1;
    }
    s_started = 1U;
    return 0;
}

/**
 * @brief 在短临界区复制本地发送计数及栈余量。
 * @param output 接收诊断快照的非空对象。
 * @return 0表示成功，非0表示参数错误或启动未完成。
 */
int AppModbus_GetSnapshot(AppModbusSnapshot *output)
{
    if (output == NULL || s_started == 0U) {
        return -1;
    }
    taskENTER_CRITICAL();
    output->trigger_count = s_snapshot.trigger_count;
    output->send_count = s_snapshot.send_count;
    output->failure_count = s_snapshot.failure_count;
    output->task_stack_free_words = s_snapshot.task_stack_free_words;
    output->last_status = s_snapshot.last_status;
    taskEXIT_CRITICAL();
    return 0;
}
