#include "app_actuator.h"
#include "FreeRTOS.h"
#include "timers.h"
#include <stdbool.h>
#include <stddef.h>
#define APP_ACTUATOR_DISABLED 0U /* 库状态契约：零为逻辑关闭。 */
#define APP_LED_SWAP_PERIOD_MS 1000U /* 每秒交换两只独立指示灯的亮灭状态。 */
#define APP_LED_COMMAND_WAIT_TICKS 0U /* 启动阶段和回调均不阻塞等待定时器队列。 */
static DevActuator *s_actuator; // 保存已初始化的执行器Device对象。
static StaticTimer_t s_led_timer_storage; // 保存静态交替闪烁定时器控制块。
static TimerHandle_t s_led_timer; // 保存唯一的交替闪烁定时器句柄。
static bool s_warning_on = false; // 保存最近成功提交的报警灯亮灭状态。
static bool s_led_blink_running = false; // 标记定时器是否允许继续交换两灯。
volatile DevStatus g_led_blink_status = DEV_NOT_READY; // 保存最近两灯控制结果，供调试器观察。
volatile DevActuatorState g_actuator_initial_state; // 保存初始化后的五路锁存逻辑快照。
/**
 * @brief 每秒交换两灯状态；设备失败时停用后续交换并尽力熄灭两灯。
 * @param timer 本模块静态软件定时器句柄。
 */
static void actuator_led_callback(TimerHandle_t timer)
{
    bool warning_on; // 保存本次拟提交的报警灯亮灭状态。
    (void)timer;
    if (!s_led_blink_running) return;
    warning_on = !s_warning_on;
    g_led_blink_status = s_actuator->setIndicatorLeds(s_actuator, warning_on, !warning_on);
    if (g_led_blink_status != DEV_OK) {
        s_led_blink_running = false;
        (void)s_actuator->setIndicatorLeds(s_actuator, false, false);
        return;
    }
    s_warning_on = warning_on;
}
/** @brief 通过Device初始化执行器并保存关闭状态快照。
 * @return 成功APP_OK，否则APP_ERROR_ACTUATOR。 */
AppStatus App_Actuator_Init(void)
{
    DevActuator *actuator = GetActuator(); // 保存本模块使用的执行器Device对象。
    DevActuatorState state; // 保存成功读取的完整状态。
    if (actuator == NULL || actuator->init(actuator) != DEV_OK ||
        actuator->getState(actuator, &state) != DEV_OK) return APP_ERROR_ACTUATOR;
    g_actuator_initial_state = state;
    if (state.relay != APP_ACTUATOR_DISABLED || state.optocoupler != APP_ACTUATOR_DISABLED ||
        state.buzzer != APP_ACTUATOR_DISABLED || state.warningLed != APP_ACTUATOR_DISABLED ||
        state.powerLed != APP_ACTUATOR_DISABLED) return APP_ERROR_ACTUATOR;
    s_actuator = actuator;
    g_led_blink_status = DEV_OK;
    return APP_OK;
}

/**
 * @brief 点亮报警灯并启动每秒交替两灯的静态定时器，重复启动不创建新对象。
 * @return 成功APP_OK，设备未就绪或控制失败APP_ERROR_ACTUATOR，定时器失败APP_ERROR_TIMER。
 */
AppStatus App_Actuator_Start(void)
{
    if (s_actuator == NULL) return APP_ERROR_ACTUATOR;
    if (s_led_blink_running) return APP_OK;
    if (s_led_timer == NULL) {
        s_led_timer = xTimerCreateStatic("led_blink", pdMS_TO_TICKS(APP_LED_SWAP_PERIOD_MS),
            pdTRUE, NULL, actuator_led_callback, &s_led_timer_storage);
    }
    if (s_led_timer == NULL) return APP_ERROR_TIMER;
    s_warning_on = true;
    g_led_blink_status = s_actuator->setIndicatorLeds(s_actuator, s_warning_on, !s_warning_on);
    if (g_led_blink_status != DEV_OK) {
        (void)s_actuator->setIndicatorLeds(s_actuator, false, false);
        return APP_ERROR_ACTUATOR;
    }
    s_led_blink_running = true;
    if (xTimerStart(s_led_timer, APP_LED_COMMAND_WAIT_TICKS) != pdPASS) {
        s_led_blink_running = false;
        g_led_blink_status = s_actuator->setIndicatorLeds(s_actuator, false, false);
        return APP_ERROR_TIMER;
    }
    return APP_OK;
}
