#include "app_buzzer.h"
#include "dev_actuator.h"
#include "FreeRTOS.h"
#include "timers.h"
#include <stddef.h>

#define APP_BUZZER_HALF_PERIOD_MS 500U
#define APP_BUZZER_BOOT_STOP_MS 2200U
#define APP_BUZZER_TIMER_COMMAND_WAIT_TICKS 0U

static StaticTimer_t s_buzzer_timer_storage; // 蜂鸣器软件定时器的静态控制块存储区。
static StaticTimer_t s_boot_stop_timer_storage; // 上电演示停止定时器的静态控制块存储区。
static TimerHandle_t s_buzzer_timer; // 蜂鸣器软件定时器句柄。
static TimerHandle_t s_boot_stop_timer; // 上电演示结束时关闭蜂鸣器的定时器句柄。
static DevActuator *s_actuator; // 蜂鸣器及执行器设备接口。
static volatile uint8_t s_buzzer_running; // 蜂鸣器当前是否处于鸣叫状态。
static uint8_t s_buzzer_output_state; // 蜂鸣器当前输出电平状态。
volatile uint8_t g_app_buzzer_demo_status; // 上电蜂鸣器演示的当前状态，供 SWD 观察。

/**
 * @brief 每经过半个周期切换一次蜂鸣器输出电平。
 * @param timer 触发本次回调的软件定时器句柄。
 */
static void buzzer_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    if (s_buzzer_running != 0U && s_actuator != NULL &&
        s_actuator->setBuzzer != NULL) {
        s_buzzer_output_state = (uint8_t)(s_buzzer_output_state == 0U ? 1U : 0U);
        if (s_actuator->setBuzzer(s_actuator, s_buzzer_output_state) == 0U) {
            s_buzzer_running = 0U;
            g_app_buzzer_demo_status = APP_BUZZER_DEMO_ERROR;
            (void)s_actuator->setBuzzer(s_actuator, 0U);
        }
    }
}

/**
 * @brief 在两次短鸣完成后停止周期定时器并关闭蜂鸣器。
 * @param timer 触发结束动作的软件定时器句柄。
 */
static void buzzer_boot_stop_callback(TimerHandle_t timer)
{
    (void)timer;
    g_app_buzzer_demo_status = AppBuzzer_Stop() != 0U ?
                               APP_BUZZER_DEMO_COMPLETE : APP_BUZZER_DEMO_ERROR;
}

/**
 * @brief 初始化执行器设备并创建静态蜂鸣器周期定时器。
 * @return 执行器和定时器初始化成功返回 1；失败返回 0。
 */
uint8_t AppBuzzer_Init(void)
{
    TickType_t period_ticks = pdMS_TO_TICKS(APP_BUZZER_HALF_PERIOD_MS); // 蜂鸣器翻转半周期对应的系统节拍数。
    TickType_t stop_ticks = pdMS_TO_TICKS(APP_BUZZER_BOOT_STOP_MS); // 上电两次短鸣结束时的系统节拍数。

    if (period_ticks == 0U || stop_ticks == 0U) {
        return 0U;
    }

    s_actuator = GetActuator();
    if (s_actuator == NULL || s_actuator->init == NULL ||
        s_actuator->setBuzzer == NULL) {
        return 0U;
    }

    if (s_actuator->init(s_actuator) == 0U ||
        s_actuator->setBuzzer(s_actuator, 0U) == 0U) {
        return 0U;
    }
    s_buzzer_output_state = 0U;
    s_buzzer_running = 0U;
    g_app_buzzer_demo_status = APP_BUZZER_DEMO_PENDING;
    s_buzzer_timer = xTimerCreateStatic(
        "buzzer", period_ticks, pdTRUE, NULL, buzzer_timer_callback,
        &s_buzzer_timer_storage);
    s_boot_stop_timer = xTimerCreateStatic(
        "buzz_stop", stop_ticks, pdFALSE, NULL, buzzer_boot_stop_callback,
        &s_boot_stop_timer_storage);
    return (s_buzzer_timer != NULL && s_boot_stop_timer != NULL) ? 1U : 0U;
}

/**
 * @brief 启动蜂鸣器并按 500 毫秒响、500 毫秒停的节奏循环切换。
 * @return 定时器启动命令入队成功返回 1；设备或定时器不可用时返回 0。
 */
uint8_t AppBuzzer_Start(void)
{
    if (s_actuator == NULL || s_buzzer_timer == NULL ||
        s_actuator->setBuzzer == NULL) {
        return 0U;
    }

    s_buzzer_output_state = 0U;
    s_buzzer_running = 1U;
    if (s_actuator->setBuzzer(s_actuator, 0U) == 0U) {
        s_buzzer_running = 0U;
        return 0U;
    }
    if (xTimerStart(s_buzzer_timer,
                    APP_BUZZER_TIMER_COMMAND_WAIT_TICKS) != pdPASS) {
        s_buzzer_running = 0U;
        (void)s_actuator->setBuzzer(s_actuator, 0U);
        return 0U;
    }
    return 1U;
}

/**
 * @brief 停止蜂鸣器周期并将输出设置为关闭状态。
 * @return 定时器停止命令入队成功返回 1；设备或定时器不可用时返回 0。
 */
uint8_t AppBuzzer_Stop(void)
{
    if (s_actuator == NULL || s_buzzer_timer == NULL ||
        s_actuator->setBuzzer == NULL) {
        return 0U;
    }

    s_buzzer_running = 0U;
    s_buzzer_output_state = 0U;
    if (s_actuator->setBuzzer(s_actuator, 0U) == 0U) {
        return 0U;
    }
    return xTimerStop(s_buzzer_timer,
                      APP_BUZZER_TIMER_COMMAND_WAIT_TICKS) == pdPASS ? 1U : 0U;
}

/**
 * @brief 上电后鸣叫两次并自动停止，验证蜂鸣器开关能力。
 * @return 启动两个软件定时器成功返回 1，否则返回 0。
 */
uint8_t AppBuzzer_StartBootDemo(void)
{
    if (s_boot_stop_timer == NULL || AppBuzzer_Start() == 0U) {
        g_app_buzzer_demo_status = APP_BUZZER_DEMO_ERROR;
        return 0U;
    }
    if (xTimerStart(s_boot_stop_timer,
                    APP_BUZZER_TIMER_COMMAND_WAIT_TICKS) != pdPASS) {
        (void)AppBuzzer_Stop();
        g_app_buzzer_demo_status = APP_BUZZER_DEMO_ERROR;
        return 0U;
    }
    g_app_buzzer_demo_status = APP_BUZZER_DEMO_RUNNING;
    return 1U;
}
