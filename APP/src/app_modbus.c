#include "app_modbus.h"
#include "dev_modbus.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#define APP_MODBUS_TASK_STACK_WORDS   128U
#define APP_MODBUS_TASK_PRIORITY      (tskIDLE_PRIORITY + 1U)
#define APP_MODBUS_PERIOD_MS          1000U
#define APP_MODBUS_SLAVE_ADDRESS      1U
#define APP_MODBUS_REGISTER_ADDRESS   0U

volatile uint32_t g_app_modbus_task_heartbeat; // Modbus APP 周期任务的调度计数。
volatile uint32_t g_app_modbus_timer_count; // Modbus APP 软件定时器回调计数。
volatile uint32_t g_app_modbus_tx_count; // 成功发送的 Modbus 请求帧数量。
volatile AppModbusTxStatus g_app_modbus_tx_status; // 最近一次 Modbus 请求发送结果。
static StaticTask_t s_modbus_task_tcb; // Modbus APP 周期任务的静态控制块。
static StackType_t s_modbus_task_stack[APP_MODBUS_TASK_STACK_WORDS]; // Modbus APP 周期任务的静态栈。
static StaticTimer_t s_modbus_timer_storage; // Modbus APP 周期软件定时器的静态存储。
static TimerHandle_t s_modbus_timer; // Modbus APP 周期软件定时器句柄。
static DevModbus *s_modbus_device; // 供 Modbus APP 周期任务调用的设备接口对象。

/**
 * @brief 每秒发送功能码 0x06 请求并更新 Modbus APP 调试心跳。
 * @param context 保留的任务上下文参数，本任务不使用。
 */
static void app_modbus_task(void *context)
{
    TickType_t wake_tick = xTaskGetTickCount(); // 周期任务上一次唤醒的内核 Tick。
    uint16_t value = 1U; // 周期写入保持寄存器 0 的递增调试数值。

    (void)context;
    for (;;) {
        vTaskDelayUntil(&wake_tick, pdMS_TO_TICKS(APP_MODBUS_PERIOD_MS));
        ++g_app_modbus_task_heartbeat;
        if (s_modbus_device != NULL &&
            s_modbus_device->writeHoldingRegister != NULL &&
            s_modbus_device->writeHoldingRegister(s_modbus_device,
                                                  APP_MODBUS_SLAVE_ADDRESS,
                                                  APP_MODBUS_REGISTER_ADDRESS,
                                                  value) == DEV_MODBUS_OK) {
            g_app_modbus_tx_status = APP_MODBUS_TX_OK;
            ++g_app_modbus_tx_count;
        } else {
            g_app_modbus_tx_status = APP_MODBUS_TX_ERROR;
        }
        ++value;
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
 * @brief 创建 Modbus APP 静态周期任务和软件定时器。
 * @param modbus Modbus 设备接口对象；串口初始化失败时任务仍可创建，但发送会报告失败。
 * @return 任务、定时器创建并成功启动返回 1，否则返回 0。
 */
uint8_t App_Modbus_Start(DevModbus *modbus)
{
    TaskHandle_t task; // 新建 Modbus APP 周期任务的句柄。

    if (modbus == NULL) {
        return 0U;
    }
    s_modbus_device = modbus;
    g_app_modbus_task_heartbeat = 0U;
    g_app_modbus_timer_count = 0U;
    g_app_modbus_tx_count = 0U;
    g_app_modbus_tx_status = APP_MODBUS_TX_NOT_RUN;
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
