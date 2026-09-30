#include "app_ir_relay.h"
#include "dev_actuator.h"
#include "dev_display.h"
#include "dev_ir_remote.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <stddef.h>

#define APP_IR_RELAY_POLL_PERIOD_MS 10U
#define APP_IR_RELAY_TASK_STACK_WORDS 192U
#define APP_IR_RELAY_TASK_PRIORITY 2U
#define APP_IR_RELAY_KEY_ON 5
#define APP_IR_RELAY_KEY_OFF 4
#define APP_IR_OPTOCOUPLER_KEY_ON 7
#define APP_IR_OPTOCOUPLER_KEY_OFF 8
#define APP_IR_RELAY_BEEP_DURATION_MS 100U
#define APP_IR_RELAY_TIMER_COMMAND_WAIT_TICKS 0U
#define APP_IR_RELAY_DISPLAY_MAX 9999U

static StaticTask_t s_ir_relay_task_tcb;
static StackType_t s_ir_relay_task_stack[APP_IR_RELAY_TASK_STACK_WORDS];
static StaticTimer_t s_ir_relay_beep_timer_storage;
static TimerHandle_t s_ir_relay_beep_timer;
static DevActuator *s_ir_relay_actuator;
static DevDisplay *s_ir_relay_display;
static uint16_t s_ir_relay_display_value;

/**
 * @brief 提示音到期后关闭蜂鸣器。
 * @param timer 已到期的软件定时器句柄，本回调不使用。
 */
static void ir_relay_beep_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    if (s_ir_relay_actuator != NULL && s_ir_relay_actuator->setBuzzer != NULL) {
        s_ir_relay_actuator->setBuzzer(s_ir_relay_actuator, 0U);
    }
}

/**
 * @brief 通过蜂鸣器短响确认红外按键已处理。
 */
static void ir_relay_beep(void)
{
    if (s_ir_relay_actuator->setBuzzer != NULL && s_ir_relay_beep_timer != NULL) {
        s_ir_relay_actuator->setBuzzer(s_ir_relay_actuator, 1U);
        if (xTimerReset(s_ir_relay_beep_timer,
                        APP_IR_RELAY_TIMER_COMMAND_WAIT_TICKS) != pdPASS) {
            s_ir_relay_actuator->setBuzzer(s_ir_relay_actuator, 0U);
        }
    }
}

/**
 * @brief 处理按键提示音、四位数值加减、继电器和光耦输出开关。
 * @param argument FreeRTOS 任务参数，本任务未使用。
 */
static void ir_relay_task(void *argument)
{
    IrKeyEvent event;
    uint8_t display_changed;

    (void)argument;
    for (;;) {
        while (DevIrRemote_ReadEvent(&event) != 0U) {
            if (event.repeat == 0U) {
                ir_relay_beep();
                display_changed = 0U;
                if (event.digit >= 0 && event.digit <= 9) {
                    s_ir_relay_display_value = (uint16_t)event.digit;
                    display_changed = 1U;
                } else if (event.action == IR_KEY_ACTION_INCREMENT) {
                    if (s_ir_relay_display_value < APP_IR_RELAY_DISPLAY_MAX) {
                        ++s_ir_relay_display_value;
                    }
                    display_changed = 1U;
                } else if (event.action == IR_KEY_ACTION_DECREMENT) {
                    if (s_ir_relay_display_value > 0U) {
                        --s_ir_relay_display_value;
                    }
                    display_changed = 1U;
                }
                if (display_changed != 0U) {
                    s_ir_relay_display->showInteger4(s_ir_relay_display,
                                                      s_ir_relay_display_value);
                }
                if (event.digit == APP_IR_RELAY_KEY_ON) {
                    s_ir_relay_actuator->setRelay(s_ir_relay_actuator, 1U);
                } else if (event.digit == APP_IR_RELAY_KEY_OFF) {
                    s_ir_relay_actuator->setRelay(s_ir_relay_actuator, 0U);
                } else if (event.digit == APP_IR_OPTOCOUPLER_KEY_ON) {
                    /* PA1 拉低时 PC817 截止，现场输出被上拉至 24 V。 */
                    s_ir_relay_actuator->setOptocoupler(s_ir_relay_actuator, 1U);
                } else if (event.digit == APP_IR_OPTOCOUPLER_KEY_OFF) {
                    /* PA1 拉高时 PC817 导通，将现场输出拉至低电平。 */
                    s_ir_relay_actuator->setOptocoupler(s_ir_relay_actuator, 0U);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(APP_IR_RELAY_POLL_PERIOD_MS));
    }
}

/**
 * @brief 初始化继电器、光耦输出、蜂鸣器、数值显示和红外解码器，并创建静态任务。
 * @return 初始化及任务创建成功返回 1；否则返回 0。
 */
uint8_t AppIrRelay_Init(void)
{
    TaskHandle_t task_handle;
    TickType_t beep_period_ticks = pdMS_TO_TICKS(APP_IR_RELAY_BEEP_DURATION_MS);

    if (beep_period_ticks == 0U) {
        return 0U;
    }

    s_ir_relay_actuator = GetActuator();
    if (s_ir_relay_actuator == NULL || s_ir_relay_actuator->init == NULL ||
        s_ir_relay_actuator->setRelay == NULL ||
        s_ir_relay_actuator->setOptocoupler == NULL ||
        s_ir_relay_actuator->setBuzzer == NULL) {
        return 0U;
    }
    s_ir_relay_display = GetDisplay();
    if (s_ir_relay_display == NULL ||
        s_ir_relay_display->showInteger4 == NULL) {
        return 0U;
    }

    s_ir_relay_display_value = 0U;
    s_ir_relay_actuator->init(s_ir_relay_actuator);
    s_ir_relay_actuator->setRelay(s_ir_relay_actuator, 0U);
    s_ir_relay_actuator->setOptocoupler(s_ir_relay_actuator, 0U);
    s_ir_relay_actuator->setBuzzer(s_ir_relay_actuator, 0U);
    s_ir_relay_beep_timer = xTimerCreateStatic(
        "ir_beep", beep_period_ticks, pdFALSE, NULL,
        ir_relay_beep_timer_callback, &s_ir_relay_beep_timer_storage);
    if (s_ir_relay_beep_timer == NULL) {
        return 0U;
    }
    DevIrRemote_Init();

    task_handle = xTaskCreateStatic(ir_relay_task, "ir_relay",
                                   APP_IR_RELAY_TASK_STACK_WORDS, NULL,
                                   APP_IR_RELAY_TASK_PRIORITY,
                                   s_ir_relay_task_stack,
                                   &s_ir_relay_task_tcb);
    return task_handle != NULL ? 1U : 0U;
}
