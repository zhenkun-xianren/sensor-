#include "app_rs485.h"
#include "app_display.h"
#include "app_settings.h"
#include "dev_modbus.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <stdbool.h>
#include <stddef.h>
#define APP_RS485_PERIOD_TICKS pdMS_TO_TICKS(500U) // 雷达查询通知周期为500毫秒。
#define APP_RS485_TASK_STACK_WORDS 384U
#define APP_RS485_TASK_PRIORITY 1U
#define APP_RS485_NO_WAIT 0U
#define APP_RS485_SLAVE_ADDRESS 1U
#define APP_RS485_REGISTER_ADDRESS 0x0000U
#define APP_RS485_REGISTER_COUNT 4U
#define APP_RS485_DISTANCE_INDEX 0U
#define APP_RS485_STATUS_INDEX 1U
#define APP_RS485_ID_INDEX 2U
#define APP_RS485_BAUD_INDEX 3U
#define APP_RS485_NORMAL_STATUS 0U
#if !DEV_MODBUS_ENABLE_READ || !DEV_MODBUS_ENABLE_RESPONSE
#error Radar_requires_Modbus_read_and_response_support
#endif
static DevModbus *s_device; // 周期读寄存器使用的设备对象。
static StaticTask_t s_task_storage; // 通信任务控制块。
static StackType_t s_task_stack[APP_RS485_TASK_STACK_WORDS]; // 静态通信任务栈。
static TaskHandle_t s_task; // 通信任务句柄。
static StaticTimer_t s_timer_storage; // 500毫秒查询通知定时器控制块。
static TimerHandle_t s_timer; // 周期通知源。
static bool s_started; // 静态资源已启动。
static bool s_device_ready; // 下个事务是否需要重新初始化串口。
static volatile DevModbusStatus s_last_result = DEV_MODBUS_NOT_INITIALIZED; // 最近协议操作结果。
static volatile uint16_t s_radar_status; // 最近有效应答中的工作状态。
static volatile uint16_t s_radar_id; // 最近有效应答中的设备地址字段。
static volatile uint16_t s_radar_baud_code; // 最近有效应答中的波特率代码。
/** @brief 通知一次查询，不在定时器回调等待串口。 @param timer 当前周期定时器。 */
static void app_rs485_timer_callback(TimerHandle_t timer)
{
 (void)timer;
 xTaskNotifyGive(s_task);
}
/** @brief 独占完整查询事务，校验完成后发布距离，失败及时使旧测量失效。 @param argument 保留任务参数。 */
static void app_rs485_send_task(void *argument)
{
 uint16_t values[APP_RS485_REGISTER_COUNT]; // 四个保持寄存器的完整输出。
 bool valid; // 本次距离是否有效。
 (void)argument;
 for (;;) {
     (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
     valid = false;
     if (App_Settings_LockBus()) {
         if (!s_device_ready) {
             s_last_result = s_device->init(s_device);
             s_device_ready = s_last_result == DEV_MODBUS_OK;
         }
         if (s_device_ready) {
             s_last_result = s_device->readHoldingRegisters(s_device,
                 APP_RS485_SLAVE_ADDRESS, APP_RS485_REGISTER_ADDRESS, APP_RS485_REGISTER_COUNT, values);
             s_device_ready = s_last_result == DEV_MODBUS_OK;
             if (s_last_result == DEV_MODBUS_OK) {
                 s_radar_status = values[APP_RS485_STATUS_INDEX];
                 s_radar_id = values[APP_RS485_ID_INDEX];
                 s_radar_baud_code = values[APP_RS485_BAUD_INDEX];
                 valid = s_radar_status == APP_RS485_NORMAL_STATUS;
             }
         }
         App_Settings_UnlockBus();
     }
     App_Display_PublishDistance(valid ? values[APP_RS485_DISTANCE_INDEX] : APP_SETTINGS_MIN_MM, valid);
 }
}
/** @brief 创建静态查询任务和500毫秒通知源，调度开始即执行首次查询。 @return 启动结果。 */
AppRs485Result App_Rs485_Start(void)
{
 if (s_started) return APP_RS485_OK;
 s_device = GetModbus();
 if (s_device == NULL || s_device->init == NULL || s_device->readHoldingRegisters == NULL)
     return APP_RS485_DEVICE_ERROR;
 s_last_result = s_device->init(s_device);
 s_device_ready = s_last_result == DEV_MODBUS_OK;
 if (!s_device_ready) return APP_RS485_DEVICE_ERROR;
 s_task = xTaskCreateStatic(app_rs485_send_task, "radar_read",
     APP_RS485_TASK_STACK_WORDS, NULL, APP_RS485_TASK_PRIORITY, s_task_stack, &s_task_storage);
 if (s_task == NULL) return APP_RS485_TASK_ERROR;
 s_timer = xTimerCreateStatic("radar_period", APP_RS485_PERIOD_TICKS,
     pdTRUE, NULL, app_rs485_timer_callback, &s_timer_storage);
 if (s_timer == NULL || xTimerStart(s_timer, APP_RS485_NO_WAIT) != pdPASS) return APP_RS485_TIMER_ERROR;
 xTaskNotifyGive(s_task);
 s_started = true;
 return APP_RS485_OK;
}
