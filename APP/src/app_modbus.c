#include "app_modbus.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <stdbool.h>
#include <stddef.h>
#define APP_MODBUS_VOLTAGE_PERIOD_MS 1000U /* 电压帧发送间隔为1秒。 */
#define APP_MODBUS_VOLTAGE_SLAVE_ADDRESS 1U /* 电压写寄存器请求的从站地址。 */
#define APP_MODBUS_VOLTAGE_REGISTER_ADDRESS 0x0000U /* 协议零基保持寄存器地址，单位mV。 */
#define APP_MODBUS_TASK_STACK_WORDS 256U /* 发送任务栈容量，单位为StackType_t字。 */
#define APP_MODBUS_TASK_PRIORITY 1U /* 低于显示定时器和红外任务优先级。 */
#define APP_MODBUS_COMMAND_WAIT_TICKS 0U /* 启动命令只尝试入队，不阻塞。 */
#define APP_MODBUS_NO_NOTIFICATION 0U /* 通知等待超时，没有待处理发送请求。 */
static DevModbus *s_modbus; // 保存唯一主站Device对象，只由发送任务执行运行期操作。
static StaticTask_t s_modbus_control; // 保存静态发送任务控制块。
static StackType_t s_modbus_stack[APP_MODBUS_TASK_STACK_WORDS]; // 保存发送任务栈。
static TaskHandle_t s_modbus_task; // 保存发送任务句柄，供定时器通知。
static StaticTimer_t s_send_storage; // 保存周期软件定时器控制块。
static TimerHandle_t s_send_timer; // 保存每秒发送定时器句柄。
volatile DevModbusStatus g_modbus_init_status = DEV_MODBUS_NOT_INITIALIZED; // 保存主站初始化结果。
volatile DevModbusStatus g_modbus_send_status = DEV_MODBUS_NOT_INITIALIZED; // 保存最近一次电压帧发送结果。
volatile uint32_t g_modbus_send_count; // 累计电压帧实际发送调用次数，自然回绕。
volatile uint32_t g_modbus_send_success_count; // 累计本地发送完成次数，不代表从站执行成功。
volatile uint32_t g_modbus_skip_count; // 统计测量失效而跳过的周期数。
volatile AppMeasurementStatus g_modbus_measurement_status = APP_MEASUREMENT_NOT_READY; // 保存最近周期测量状态。
volatile uint16_t g_modbus_last_millivolts; // 保存最近一次实际提交发送的毫伏值。
/**
 * @brief 每秒通知发送任务；回调不执行串口等待，避免影响显示扫描。
 * @param timer 本模块静态周期定时器句柄。
 */
static void modbus_send_callback(TimerHandle_t timer)
{
    (void)timer;
    (void)xTaskNotifyGive(s_modbus_task);
}
/**
 * @brief 每周期读取有效测量并发送0x06毫伏帧；通信失败后在下一周期重新初始化。
 * @param argument 未使用的静态任务参数。
 */
static void modbus_send_task(void *argument)
{
    bool reinit_required = false; // 标记底层发送失败后是否需要恢复设备。
    AppMeasurementSnapshot snapshot; // 保存本周期将发送的完整测量快照。
    (void)argument;
    for (;;) {
        /* 清除累计通知，每次唤醒只发送一帧，避免延迟后连续补发。 */
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == APP_MODBUS_NO_NOTIFICATION) continue;
        g_modbus_measurement_status = App_Measurement_GetLatest(&snapshot);
        if (g_modbus_measurement_status != APP_MEASUREMENT_OK) {
            ++g_modbus_skip_count;
            continue; // 不将旧值、零占位值或失效测量发送给从站。
        }
        if (reinit_required) {
            g_modbus_init_status = s_modbus->init(s_modbus);
            if (g_modbus_init_status != DEV_MODBUS_OK) {
                g_modbus_send_status = g_modbus_init_status;
                continue;
            }
            reinit_required = false;
        }
        /* 通信重新初始化可能消耗时间，发送前再次检查快照是否仍有效。 */
        g_modbus_measurement_status = App_Measurement_GetLatest(&snapshot);
        if (g_modbus_measurement_status != APP_MEASUREMENT_OK) {
            ++g_modbus_skip_count;
            continue;
        }
        g_modbus_last_millivolts = snapshot.millivolts;
        g_modbus_send_status = s_modbus->sendHoldingRegister(s_modbus,
            APP_MODBUS_VOLTAGE_SLAVE_ADDRESS, APP_MODBUS_VOLTAGE_REGISTER_ADDRESS,
            snapshot.millivolts);
        ++g_modbus_send_count;
        if (g_modbus_send_status == DEV_MODBUS_OK) {
            ++g_modbus_send_success_count;
        } else {
            reinit_required = (g_modbus_send_status == DEV_MODBUS_ERROR_IO ||
                g_modbus_send_status == DEV_MODBUS_ERROR_TIMEOUT ||
                g_modbus_send_status == DEV_MODBUS_NOT_INITIALIZED);
        }
    }
}
/** @brief 初始化Modbus Device并检查测试发送所需的方法。
 * @return 成功APP_OK，否则APP_ERROR_MODBUS。 */
AppStatus App_Modbus_Init(void)
{
    s_modbus = GetModbus();
    if (s_modbus == NULL || s_modbus->init == NULL ||
        s_modbus->sendHoldingRegister == NULL) return APP_ERROR_MODBUS;
    g_modbus_init_status = s_modbus->init(s_modbus);
    return g_modbus_init_status == DEV_MODBUS_OK ? APP_OK : APP_ERROR_MODBUS;
}
/**
 * @brief 启动阶段创建静态发送任务和1秒定时器，首次发送在调度开始约1秒后。
 * @return 成功APP_OK，设备未就绪APP_ERROR_MODBUS，任务失败APP_ERROR_TASK，定时器失败APP_ERROR_TIMER。
 */
AppStatus App_Modbus_Start(void)
{
    if (s_modbus == NULL || g_modbus_init_status != DEV_MODBUS_OK) return APP_ERROR_MODBUS;
    s_modbus_task = xTaskCreateStatic(modbus_send_task, "rs485_voltage",
        APP_MODBUS_TASK_STACK_WORDS, NULL, APP_MODBUS_TASK_PRIORITY,
        s_modbus_stack, &s_modbus_control);
    if (s_modbus_task == NULL) return APP_ERROR_TASK;
    s_send_timer = xTimerCreateStatic("rs485_1s", pdMS_TO_TICKS(APP_MODBUS_VOLTAGE_PERIOD_MS),
        pdTRUE, NULL, modbus_send_callback, &s_send_storage);
    if (s_send_timer == NULL ||
        xTimerStart(s_send_timer, APP_MODBUS_COMMAND_WAIT_TICKS) != pdPASS) return APP_ERROR_TIMER;
    return APP_OK;
}
