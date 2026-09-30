#include "app_led.h"
#include "dev_actuator.h"
#include "FreeRTOS.h"
#include "timers.h"
#include <stddef.h>

#define APP_LED_SWITCH_PERIOD_MS 1000U
#define APP_LED_TIMER_COMMAND_WAIT_TICKS 0U

static StaticTimer_t s_led_timer_storage;
static TimerHandle_t s_led_timer;
static DevActuator *s_actuator;
static uint8_t s_warning_led_on;

/**
 * @brief 每秒在 WARNING 与 POWER 指示灯之间切换点亮状态。
 * @param timer 触发本次回调的软件定时器句柄。
 */
static void led_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    if (s_actuator == NULL || s_actuator->setIndicatorLeds == NULL) {
        return;
    }

    s_warning_led_on = (uint8_t)(s_warning_led_on == 0U ? 1U : 0U);
    s_actuator->setIndicatorLeds(s_actuator, s_warning_led_on,
                                 s_warning_led_on == 0U ? 1U : 0U);
}

/**
 * @brief 初始化指示灯输出并创建静态周期定时器。
 * @return 设备与定时器初始化成功返回 1；失败返回 0。
 */
uint8_t AppLed_Init(void)
{
    TickType_t period_ticks = pdMS_TO_TICKS(APP_LED_SWITCH_PERIOD_MS);

    if (period_ticks == 0U) {
        return 0U;
    }

    s_actuator = GetActuator();
    if (s_actuator == NULL || s_actuator->setIndicatorLeds == NULL) {
        return 0U;
    }

    s_warning_led_on = 1U;
    s_actuator->setIndicatorLeds(s_actuator, 1U, 0U);
    
    s_led_timer = xTimerCreateStatic("led_alt", period_ticks, pdTRUE, NULL,
                                     led_timer_callback, &s_led_timer_storage);
    if (s_led_timer == NULL ||
        xTimerStart(s_led_timer, APP_LED_TIMER_COMMAND_WAIT_TICKS) != pdPASS) {
        s_actuator->setIndicatorLeds(s_actuator, 0U, 0U);
        return 0U;
    }
    return 1U;
}
