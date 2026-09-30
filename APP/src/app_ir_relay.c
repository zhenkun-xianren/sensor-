#include "app_ir_relay.h"
#include "dev_actuator.h"
#include "dev_display.h"
#include "dev_ir_remote.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <stddef.h>

#define APP_IR_RELAY_POLL_PERIOD_MS          10U
#define APP_IR_RELAY_TASK_STACK_WORDS       192U
#define APP_IR_RELAY_TASK_PRIORITY            2U
#define APP_IR_RELAY_KEY_RELAY_ON             5
#define APP_IR_RELAY_KEY_RELAY_OFF            4
#define APP_IR_RELAY_KEY_OPTO_ON              7
#define APP_IR_RELAY_KEY_OPTO_OFF             8
#define APP_IR_RELAY_BEEP_DURATION_MS       100U
#define APP_IR_RELAY_TIMER_COMMAND_WAIT_TICKS  0U
#define APP_IR_RELAY_DISPLAY_MAX            9999U

static StaticTask_t s_ir_relay_task_tcb; // 红外控制任务的静态任务控制块。
static StackType_t s_ir_relay_task_stack[APP_IR_RELAY_TASK_STACK_WORDS]; // 红外控制任务的静态栈空间。
static StaticTimer_t s_ir_relay_beep_timer_storage; // 按键提示音定时器的静态控制块存储区。
static TimerHandle_t s_ir_relay_beep_timer; // 控制按键提示音时长的一次性定时器句柄。
static DevActuator *s_ir_relay_actuator; // 继电器、隔离输出和蜂鸣器设备接口。
static DevDisplay *s_ir_relay_display; // 红外控制模块使用的数码管设备接口。
static DevIrRemote *s_ir_relay_remote; // NEC 红外事件读取设备接口。
static uint16_t s_ir_relay_display_value; // 当前显示的四位数值。
volatile uint8_t g_app_ir_relay_status; // 红外控制模块的初始化和运行状态，供 SWD 观察。

/**
 * @brief 发生控制失败时释放继电器、关闭隔离输出和蜂鸣器。
 */
static void ir_relay_enter_safe_error(void)
{
    uint8_t relay_off_ok = 0U; // 继电器已确认释放时为非零。
    uint8_t opto_off_ok = 0U; // 现场隔离输出已确认关闭时为非零。
    uint8_t buzzer_off_ok = 0U; // 蜂鸣器已确认关闭时为非零。

    if (s_ir_relay_actuator != NULL) {
        if (s_ir_relay_actuator->setRelay != NULL) {
            relay_off_ok = s_ir_relay_actuator->setRelay(s_ir_relay_actuator, 0U);
        }
        if (s_ir_relay_actuator->setOptocoupler != NULL) {
            opto_off_ok = s_ir_relay_actuator->setOptocoupler(s_ir_relay_actuator, 0U);
        }
        if (s_ir_relay_actuator->setBuzzer != NULL) {
            buzzer_off_ok = s_ir_relay_actuator->setBuzzer(s_ir_relay_actuator, 0U);
        }
    }
    g_app_ir_relay_status = (relay_off_ok != 0U && opto_off_ok != 0U &&
                             buzzer_off_ok != 0U) ?
                            APP_IR_RELAY_STATUS_SAFE :
                            APP_IR_RELAY_STATUS_SAFE_FAILURE;
}

/**
 * @brief 按键提示音到期后关闭蜂鸣器。
 * @param timer 触发本次回调的软件定时器句柄，本回调不使用。
 */
static void ir_relay_beep_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    if (s_ir_relay_actuator == NULL || s_ir_relay_actuator->setBuzzer == NULL ||
        s_ir_relay_actuator->setBuzzer(s_ir_relay_actuator, 0U) == 0U) {
        g_app_ir_relay_status = APP_IR_RELAY_STATUS_ERROR;
    }
}

/**
 * @brief 开启按键提示音，并安排一次性定时器在指定时长后关断。
 * @return 蜂鸣器和定时器操作均成功返回 1，否则返回 0。
 */
static uint8_t ir_relay_beep(void)
{
    if (s_ir_relay_actuator == NULL || s_ir_relay_actuator->setBuzzer == NULL ||
        s_ir_relay_beep_timer == NULL ||
        s_ir_relay_actuator->setBuzzer(s_ir_relay_actuator, 1U) == 0U) {
        return 0U;
    }
    if (xTimerReset(s_ir_relay_beep_timer,
                    APP_IR_RELAY_TIMER_COMMAND_WAIT_TICKS) != pdPASS) {
        if (s_ir_relay_actuator->setBuzzer(s_ir_relay_actuator, 0U) == 0U) {
            g_app_ir_relay_status = APP_IR_RELAY_STATUS_ERROR;
        }
        return 0U;
    }
    return 1U;
}

/**
 * @brief 更新当前数值并执行红外按键对应的输出控制。
 * @param event 已解码的红外遥控按键事件。
 * @return 按键提示、数码管及执行器处理均成功返回 1，否则返回 0。
 */
static uint8_t ir_relay_process_event(const IrKeyEvent *event)
{
    uint8_t display_changed = 0U; // 本次按键是否需要刷新数码管数值。

    if (event == NULL || event->repeat != 0U) {
        return 1U;
    }
    if (ir_relay_beep() == 0U) {
        return 0U;
    }

    if (event->digit >= 0 && event->digit <= 9) {
        s_ir_relay_display_value = (uint16_t)event->digit;
        display_changed = 1U;
    } else if (event->action == IR_KEY_ACTION_INCREMENT) {
        if (s_ir_relay_display_value < APP_IR_RELAY_DISPLAY_MAX) {
            ++s_ir_relay_display_value;
        }
        display_changed = 1U;
    } else if (event->action == IR_KEY_ACTION_DECREMENT) {
        if (s_ir_relay_display_value > 0U) {
            --s_ir_relay_display_value;
        }
        display_changed = 1U;
    }

    if (display_changed != 0U) {
        if (s_ir_relay_display == NULL || s_ir_relay_display->showInteger4 == NULL) {
            return 0U;
        }
        s_ir_relay_display->showInteger4(s_ir_relay_display,
                                          s_ir_relay_display_value);
    }

    if (event->digit == APP_IR_RELAY_KEY_RELAY_ON) {
        return s_ir_relay_actuator->setRelay(s_ir_relay_actuator, 1U);
    }
    if (event->digit == APP_IR_RELAY_KEY_RELAY_OFF) {
        return s_ir_relay_actuator->setRelay(s_ir_relay_actuator, 0U);
    }
    if (event->digit == APP_IR_RELAY_KEY_OPTO_ON) {
        return s_ir_relay_actuator->setOptocoupler(s_ir_relay_actuator, 1U);
    }
    if (event->digit == APP_IR_RELAY_KEY_OPTO_OFF) {
        return s_ir_relay_actuator->setOptocoupler(s_ir_relay_actuator, 0U);
    }
    return 1U;
}

/**
 * @brief 读取并处理红外按键事件，未有按键时按周期阻塞等待。
 * @param argument FreeRTOS 任务参数，本任务未使用。
 */
static void ir_relay_task(void *argument)
{
    IrKeyEvent event; // 本轮从红外设备队列取出的按键事件。

    (void)argument;
    for (;;) {
        if (g_app_ir_relay_status == APP_IR_RELAY_STATUS_READY) {
            while (g_app_ir_relay_status == APP_IR_RELAY_STATUS_READY &&
                   s_ir_relay_remote->readEvent(s_ir_relay_remote, &event) != 0U) {
                if (ir_relay_process_event(&event) == 0U) {
                    ir_relay_enter_safe_error();
                    break;
                }
            }
        } else if (g_app_ir_relay_status == APP_IR_RELAY_STATUS_ERROR) {
            ir_relay_enter_safe_error();
        }
        vTaskDelay(pdMS_TO_TICKS(APP_IR_RELAY_POLL_PERIOD_MS));
    }
}

/**
 * @brief 初始化红外接收、执行器、按键提示定时器和静态控制任务。
 * @return 板级红外输入、设备初始化和静态资源创建均成功返回 1，否则返回 0。
 */
uint8_t AppIrRelay_Init(void)
{
    TaskHandle_t task_handle; // 成功创建后返回的红外控制任务句柄。
    TickType_t beep_period_ticks = pdMS_TO_TICKS(APP_IR_RELAY_BEEP_DURATION_MS); // 按键提示音时长对应的系统节拍数。

    g_app_ir_relay_status = APP_IR_RELAY_STATUS_PENDING;
    if (beep_period_ticks == 0U) {
        return 0U;
    }
    s_ir_relay_actuator = GetActuator();
    s_ir_relay_display = GetDisplay();
    s_ir_relay_remote = GetIrRemote();
    if (s_ir_relay_actuator == NULL || s_ir_relay_actuator->init == NULL ||
        s_ir_relay_actuator->setRelay == NULL ||
        s_ir_relay_actuator->setOptocoupler == NULL ||
        s_ir_relay_actuator->setBuzzer == NULL ||
        s_ir_relay_display == NULL || s_ir_relay_display->showInteger4 == NULL ||
        s_ir_relay_remote == NULL || s_ir_relay_remote->init == NULL ||
        s_ir_relay_remote->readEvent == NULL) {
        return 0U;
    }

    if (s_ir_relay_actuator->init(s_ir_relay_actuator) == 0U ||
        s_ir_relay_actuator->setRelay(s_ir_relay_actuator, 0U) == 0U ||
        s_ir_relay_actuator->setOptocoupler(s_ir_relay_actuator, 0U) == 0U ||
        s_ir_relay_actuator->setBuzzer(s_ir_relay_actuator, 0U) == 0U ||
        s_ir_relay_remote->init(s_ir_relay_remote) == 0U) {
        ir_relay_enter_safe_error();
        return 0U;
    }
    s_ir_relay_display_value = 0U;
    s_ir_relay_display->showInteger4(s_ir_relay_display, s_ir_relay_display_value);

    s_ir_relay_beep_timer = xTimerCreateStatic(
        "ir_beep", beep_period_ticks, pdFALSE, NULL,
        ir_relay_beep_timer_callback, &s_ir_relay_beep_timer_storage);
    if (s_ir_relay_beep_timer == NULL) {
        ir_relay_enter_safe_error();
        return 0U;
    }
    task_handle = xTaskCreateStatic(ir_relay_task, "ir_relay",
                                    APP_IR_RELAY_TASK_STACK_WORDS, NULL,
                                    APP_IR_RELAY_TASK_PRIORITY,
                                    s_ir_relay_task_stack,
                                    &s_ir_relay_task_tcb);
    if (task_handle == NULL) {
        ir_relay_enter_safe_error();
        return 0U;
    }
    g_app_ir_relay_status = APP_IR_RELAY_STATUS_READY;
    return 1U;
}
