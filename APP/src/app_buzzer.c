#include "app_buzzer.h"
#include "dev_actuator.h"
#include "FreeRTOS.h"
#include "timers.h"
#include <stddef.h>

#define APP_BUZZER_HALF_PERIOD_MS 500U
#define APP_BUZZER_TIMER_COMMAND_WAIT_TICKS 0U

static StaticTimer_t s_buzzer_timer_storage;
static TimerHandle_t s_buzzer_timer;
static DevActuator *s_actuator;
static volatile uint8_t s_buzzer_running;
static uint8_t s_buzzer_output_state;

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
        s_actuator->setBuzzer(s_actuator, s_buzzer_output_state);
    }
}

/**
 * @brief 初始化执行器设备并创建静态蜂鸣器周期定时器。
 * @return 执行器和定时器初始化成功返回 1；失败返回 0。
 */
uint8_t AppBuzzer_Init(void)
{
    TickType_t period_ticks = pdMS_TO_TICKS(APP_BUZZER_HALF_PERIOD_MS);

    if (period_ticks == 0U) {
        return 0U;
    }

    s_actuator = GetActuator();
    if (s_actuator == NULL || s_actuator->init == NULL ||
        s_actuator->setBuzzer == NULL) {
        return 0U;
    }

    s_actuator->init(s_actuator);
    s_actuator->setBuzzer(s_actuator, 0U);
    s_buzzer_output_state = 0U;
    s_buzzer_running = 0U;
    s_buzzer_timer = xTimerCreateStatic(
        "buzzer", period_ticks, pdTRUE, NULL, buzzer_timer_callback,
        &s_buzzer_timer_storage);
    return s_buzzer_timer != NULL ? 1U : 0U;
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
    s_actuator->setBuzzer(s_actuator, 0U);
    if (xTimerStart(s_buzzer_timer,
                    APP_BUZZER_TIMER_COMMAND_WAIT_TICKS) != pdPASS) {
        s_buzzer_running = 0U;
        s_actuator->setBuzzer(s_actuator, 0U);
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
    s_actuator->setBuzzer(s_actuator, 0U);
    return xTimerStop(s_buzzer_timer,
                      APP_BUZZER_TIMER_COMMAND_WAIT_TICKS) == pdPASS ? 1U : 0U;
}
