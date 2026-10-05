#include "app_rs485.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <stddef.h>

#define APP_RS485_PERIOD_MS          1000U
#define APP_RS485_SLAVE_ADDRESS         1U
#define APP_RS485_REGISTER_ADDRESS 0x0000U
#define APP_RS485_TASK_STACK_WORDS    256U
#define APP_RS485_TASK_PRIORITY         1U
#define APP_RS485_TIMER_COMMAND_WAIT    0U

static StaticTask_t s_task_tcb; // 通信任务静态控制块。
static StackType_t s_task_stack[APP_RS485_TASK_STACK_WORDS]; // 通信任务静态栈，以32位字计。
static TaskHandle_t s_task_handle; // 一秒通知的目标任务。
static StaticTimer_t s_timer_tcb; // 一秒自动重装定时器静态控制块。
static TimerHandle_t s_timer; // 一秒软件定时器句柄。
volatile AppRs485Status g_app_rs485_status; // 模块启动与运行状态。
volatile DevModbusStatus g_app_rs485_last_result; // 最近事务结束的状态。
volatile uint16_t g_app_rs485_last_value; // 最近发起的测试值。
volatile uint32_t g_app_rs485_attempt_count; // 发起请求总数。
volatile uint32_t g_app_rs485_success_count; // 正常回显总数。
volatile uint32_t g_app_rs485_timeout_count; // 超时总数。
volatile uint32_t g_app_rs485_error_count; // 其他通信错误总数。
volatile uint32_t g_app_rs485_stack_free_words; // 历史最小剩余栈字数。

/**
 * @brief 仅通知任务，不在 Timer Service Task 中执行收发或等待。
 * @param timer 触发通知的定时器，本函数不使用其值。
 */
static void rs485_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    if (s_task_handle != NULL) {
        (void)xTaskNotifyGive(s_task_handle);
    }
}

/**
 * @brief 停止本模块并保留错误状态，让其他任务继续运行。
 * @param status 初始化失败阶段。
 */
static void rs485_stop(AppRs485Status status)
{
    g_app_rs485_status = status;
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}

/**
 * @brief 初始化设备后每秒执行一次递增值写事务。
 * @param context 保留的任务参数，本任务不使用。
 */
static void rs485_task(void *context)
{
    DevModbus *device = GetModbus(); // 唯一拥有 Modbus 设备的业务任务。
    uint16_t value = 0U; // 下一次事务将使用的测试值。
    DevModbusStatus result; // 当前初始化或通信结果。
    (void)context;
    g_app_rs485_status = APP_RS485_INITIALIZING;
    if (device == NULL || device->init == NULL || device->writeHoldingRegister == NULL) {
        rs485_stop(APP_RS485_ERROR_DEVICE);
    }
    result = device->init(device);
    g_app_rs485_last_result = result;
    if (result != DEV_MODBUS_OK) {
        rs485_stop(APP_RS485_ERROR_INIT);
    }
    if (xTimerStart(s_timer, APP_RS485_TIMER_COMMAND_WAIT) != pdPASS) {
        rs485_stop(APP_RS485_ERROR_TIMER);
    }
    g_app_rs485_status = APP_RS485_RUNNING;
    for (;;) {
        /* 合并已积压通知，事务期间新到的通知在完成后丢弃，不补发。 */
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        g_app_rs485_last_value = value;
        ++g_app_rs485_attempt_count;
        result = device->writeHoldingRegister(device, APP_RS485_SLAVE_ADDRESS,
            APP_RS485_REGISTER_ADDRESS, value);
        value = (uint16_t)(value + 1U);
        g_app_rs485_last_result = result;
        if (result == DEV_MODBUS_OK) {
            ++g_app_rs485_success_count;
        } else if (result == DEV_MODBUS_ERROR_TIMEOUT) {
            ++g_app_rs485_timeout_count;
        } else {
            ++g_app_rs485_error_count;
        }
        g_app_rs485_stack_free_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        (void)ulTaskNotifyTake(pdTRUE, 0U);
    }
}

/**
 * @brief 创建通信任务和静态一秒周期定时器。
 * @return 创建成功返回 1，资源或周期无效返回 0。
 */
uint8_t AppRs485_Start(void)
{
    TickType_t period = pdMS_TO_TICKS(APP_RS485_PERIOD_MS); // 一秒对应的整数 Tick 数。
    g_app_rs485_status = APP_RS485_PENDING;
    g_app_rs485_last_result = DEV_MODBUS_NOT_INITIALIZED;
    g_app_rs485_last_value = 0U;
    g_app_rs485_attempt_count = 0U;
    g_app_rs485_success_count = 0U;
    g_app_rs485_timeout_count = 0U;
    g_app_rs485_error_count = 0U;
    g_app_rs485_stack_free_words = 0U;
    if (period == 0U) {
        g_app_rs485_status = APP_RS485_ERROR_RESOURCE;
        return 0U;
    }
    s_timer = xTimerCreateStatic("rs485_1s", period, pdTRUE, NULL,
        rs485_timer_callback, &s_timer_tcb);
    s_task_handle = xTaskCreateStatic(rs485_task, "rs485", APP_RS485_TASK_STACK_WORDS,
        NULL, APP_RS485_TASK_PRIORITY, s_task_stack, &s_task_tcb);
    if (s_timer == NULL || s_task_handle == NULL) {
        g_app_rs485_status = APP_RS485_ERROR_RESOURCE;
        return 0U;
    }
    return 1U;
}

