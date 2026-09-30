#include "app_modbus.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#define APP_MODBUS_TIMER_PERIOD_MS       1000U
#define APP_MODBUS_TASK_STACK_WORDS      256U
#define APP_MODBUS_TASK_PRIORITY         2U
#define APP_MODBUS_SLAVE_ADDRESS         1U
#define APP_MODBUS_REGISTER_ADDRESS     0x0000U

static StaticTask_t s_modbus_task_tcb;
static StackType_t s_modbus_task_stack[APP_MODBUS_TASK_STACK_WORDS];
static StaticTimer_t s_modbus_timer_storage;
static TaskHandle_t s_modbus_task_handle;
static TimerHandle_t s_modbus_timer_handle;
static DevModbus *s_modbus_device;
static volatile DevModbusStatus s_last_status = DEV_MODBUS_NOT_INITIALIZED;

/**
 * @brief 将软件定时器到期事件通知 Modbus 事务任务。
 * @param timer 已到期的周期定时器句柄，本回调不使用该参数。
 */
static void modbus_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    if (s_modbus_task_handle != 0) {
        xTaskNotifyGive(s_modbus_task_handle);
    }
}

/**
 * @brief 按周期通知执行一次 Modbus 保持寄存器写事务。
 * @param argument FreeRTOS 任务参数，本任务未使用。
 */
static void modbus_task(void *argument)
{
    uint16_t value = 0U;

    (void)argument;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_modbus_device != 0 && s_modbus_device->writeHoldingRegister != 0) {
            s_last_status = s_modbus_device->writeHoldingRegister(
                s_modbus_device, APP_MODBUS_SLAVE_ADDRESS,
                APP_MODBUS_REGISTER_ADDRESS, value);
            value = (uint16_t)(value + 1U);
        } else {
            s_last_status = DEV_MODBUS_NOT_INITIALIZED;
        }
    }
}

/**
 * @brief 启动 Modbus RTU 周期写寄存器模块。
 * @return 设备初始化、静态任务创建和软件定时器启动均成功时返回 1，否则返回 0。
 */
uint8_t AppModbus_Init(void)
{
    TickType_t periodTicks = pdMS_TO_TICKS(APP_MODBUS_TIMER_PERIOD_MS);
    TaskHandle_t taskHandle;

    if (periodTicks == 0U) {
        return 0U;
    }

    s_modbus_device = GetModbus();
    if (s_modbus_device == 0 || s_modbus_device->init == 0 ||
        s_modbus_device->writeHoldingRegister == 0 ||
        s_modbus_device->init(s_modbus_device) != DEV_MODBUS_OK) {
        s_last_status = DEV_MODBUS_ERROR_IO;
        return 0U;
    }

    taskHandle = xTaskCreateStatic(modbus_task, "modbus",
                                   APP_MODBUS_TASK_STACK_WORDS, 0,
                                   APP_MODBUS_TASK_PRIORITY,
                                   s_modbus_task_stack,
                                   &s_modbus_task_tcb);
    if (taskHandle == 0) {
        s_last_status = DEV_MODBUS_ERROR_IO;
        return 0U;
    }
    s_modbus_task_handle = taskHandle;

    s_modbus_timer_handle = xTimerCreateStatic(
        "mb_tx", periodTicks, pdTRUE, 0, modbus_timer_callback,
        &s_modbus_timer_storage);
    if (s_modbus_timer_handle == 0 ||
        xTimerStart(s_modbus_timer_handle, 0U) != pdPASS) {
        s_last_status = DEV_MODBUS_ERROR_IO;
        return 0U;
    }

    s_last_status = DEV_MODBUS_OK;
    return 1U;
}

/**
 * @brief 读取最近一次周期写寄存器事务的结果。
 * @return 最近一次事务的 Modbus 状态；模块尚未启动时返回 DEV_MODBUS_NOT_INITIALIZED。
 */
DevModbusStatus AppModbus_GetLastStatus(void)
{
    return s_last_status;
}
