#include "app_ir.h"
#include "dev_display.h"
#include "dev_actuator.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <stddef.h>
#define APP_IR_POLL_MS 10U
#define APP_IR_TASK_STACK_WORDS 256U
#define APP_IR_TASK_PRIORITY 2U
#define APP_IR_MAX_EVENTS_PER_POLL 8U
#define APP_IR_FIRST_DIGIT 1
#define APP_IR_LAST_DIGIT 9
#define APP_IR_RELAY_OFF_DIGIT 4
#define APP_IR_RELAY_ON_DIGIT 5
#define APP_IR_OPTOCOUPLER_OFF_DIGIT 7
#define APP_IR_OPTOCOUPLER_ON_DIGIT 8
#define APP_IR_COMPLETE_FRAME 0U /* Device契约：完整帧为零，长按重复帧为非零。 */
#define APP_IR_INTEGER_DECIMAL_PLACES 0U /* 显示整数，不显示小数点。 */
#define APP_IR_ACTUATOR_DISABLED 0U /* Device契约：零请求逻辑关闭。 */
#define APP_IR_ACTUATOR_ENABLED 1U /* Device契约：非零请求逻辑开启。 */
#define APP_IR_BEEP_MS 100U
#define APP_IR_TIMER_COMMAND_WAIT_TICKS 0U /* 定时器命令只尝试入队，不阻塞。 */
static DevIrRemote *s_ir; // 保存红外Device对象。
static DevDisplay *s_display; // 保存已由显示模块初始化的Device对象。
static DevActuator *s_actuator; // 保存已由执行器模块初始化的Device对象。
static StaticTimer_t s_beep_storage; // 保存蜂鸣器单次软件定时器控制块。
static TimerHandle_t s_beep_timer; // 保存100ms蜂鸣关闭定时器句柄。
static TickType_t s_beep_started; // 保存最近一次有效按键开启蜂鸣器的Tick。
static bool s_beep_active = false; // 标记蜂鸣器是否有尚未结束的鸣叫请求。
static StaticTask_t s_ir_control; // 保存红外静态任务控制块。
static StackType_t s_ir_stack[APP_IR_TASK_STACK_WORDS]; // 保存任务栈，单位为StackType_t字。
static TaskHandle_t s_ir_task; // 保存任务句柄，供用户检查栈余量。
volatile IrKeyEvent g_ir_last_event; // 保存最近有效事件，供调试器观察。
volatile bool g_ir_has_event = false; // 记录是否曾收到完整事件。
volatile uint32_t g_ir_overflow_count; // 保存Device队列累计丢弃数量。
volatile DevIrResult g_ir_status = DEV_IR_ERROR_NOT_READY; // 保存最近读取状态。
volatile DevDisplayStatus g_ir_display_status = DEV_DISPLAY_NOT_INITIALIZED; // 保存数字显示请求结果。
volatile DevStatus g_ir_relay_status = DEV_NOT_READY; // 保存最近继电器请求结果，不代表触点反馈。
volatile DevStatus g_ir_optocoupler_status = DEV_NOT_READY; // 保存最近光耦开关请求结果，不代表负载反馈。
volatile DevStatus g_ir_buzzer_status = DEV_NOT_READY; // 保存蜂鸣器开关请求结果。
volatile AppStatus g_ir_feedback_status = APP_OK; // 保存蜂鸣请求及定时器命令提交结果。
/**
 * @brief 到期关闭蜂鸣器；与红外任务通过短临界区保护最近鸣叫请求。
 * @param timer 本模块单次蜂鸣软件定时器句柄。
 */
static void beep_stop_callback(TimerHandle_t timer)
{
    (void)timer;
    taskENTER_CRITICAL();
    /* 新按键已成功排队重置命令时，旧到期回调不能提前结束新鸣叫。
       返回后Timer Service会处理该重置命令，并重新安排100ms到期。 */
    if (s_beep_active &&
        (TickType_t)(xTaskGetTickCount() - s_beep_started) >= pdMS_TO_TICKS(APP_IR_BEEP_MS)) {
        g_ir_buzzer_status = s_actuator->setBuzzer(s_actuator, APP_IR_ACTUATOR_DISABLED);
        s_beep_active = false;
        if (g_ir_buzzer_status != DEV_OK) g_ir_feedback_status = APP_ERROR_ACTUATOR;
    }
    taskEXIT_CRITICAL();
}
/**
 * @brief 开启100ms非阻塞蜂鸣；再次按键从本次请求重新计时。
 * @return 成功APP_OK，输出失败APP_ERROR_ACTUATOR，命令入队失败APP_ERROR_TIMER。
 */
static AppStatus beep_start(void)
{
    AppStatus result = APP_OK; // 保存蜂鸣输出及关闭定时器的提交结果。
    taskENTER_CRITICAL();
    g_ir_buzzer_status = s_actuator->setBuzzer(s_actuator, APP_IR_ACTUATOR_ENABLED);
    if (g_ir_buzzer_status != DEV_OK) {
        result = APP_ERROR_ACTUATOR;
    } else {
        s_beep_started = xTaskGetTickCount();
        s_beep_active = true;
        if (xTimerReset(s_beep_timer, APP_IR_TIMER_COMMAND_WAIT_TICKS) != pdPASS) {
            /* 没有成功安排关闭时，立即关闭输出，避免蜂鸣持续开启。 */
            g_ir_buzzer_status = s_actuator->setBuzzer(s_actuator, APP_IR_ACTUATOR_DISABLED);
            s_beep_active = false;
            result = APP_ERROR_TIMER;
        }
    }
    taskEXIT_CRITICAL();
    return result;
}
/**
 * @brief 处理数字1至9：显示整数、蜂鸣100ms，4关5开继电器，7关8开光耦。
 * @param event 本次读取的完整红外事件；长按重复帧和其他数字不执行动作。
 */
static void handle_digit(const IrKeyEvent *event)
{
    if (event->repeat != APP_IR_COMPLETE_FRAME ||
        event->digit < APP_IR_FIRST_DIGIT || event->digit > APP_IR_LAST_DIGIT) return;
    g_ir_display_status = s_display->showFixedPoint(s_display, (uint16_t)event->digit,
        APP_IR_INTEGER_DECIMAL_PLACES, DEV_DISPLAY_LEADING_ZERO_SUPPRESS);
    if (event->digit == APP_IR_RELAY_OFF_DIGIT) {
        g_ir_relay_status = s_actuator->setRelay(s_actuator, APP_IR_ACTUATOR_DISABLED);
    } else if (event->digit == APP_IR_RELAY_ON_DIGIT) {
        g_ir_relay_status = s_actuator->setRelay(s_actuator, APP_IR_ACTUATOR_ENABLED);
    } else if (event->digit == APP_IR_OPTOCOUPLER_OFF_DIGIT) {
        g_ir_optocoupler_status = s_actuator->setOptocoupler(s_actuator, APP_IR_ACTUATOR_DISABLED);
    } else if (event->digit == APP_IR_OPTOCOUPLER_ON_DIGIT) {
        /* Device开启请求由底层转换为GPIO低电平，使光耦开启。 */
        g_ir_optocoupler_status = s_actuator->setOptocoupler(s_actuator, APP_IR_ACTUATOR_ENABLED);
    }
    g_ir_feedback_status = beep_start();
}
/** @brief 每10ms读取并处理数字键；每轮有界排空避免噪声持续入队时饥饿。
 * @param argument 未使用的静态任务参数。 */
static void ir_task(void *argument)
{
    TickType_t last = xTaskGetTickCount(); // 保存周期调度基准。
    IrKeyEvent event; // 保存本次取出的完整事件。
    uint32_t overflow; // 保存本次Device返回的溢出计数。
    uint8_t count; // 限定每次轮询的读取次数。
    (void)argument;
    for (;;) {
        for (count = 0U; count < APP_IR_MAX_EVENTS_PER_POLL; ++count) {
            g_ir_status = s_ir->read_event(s_ir, &event);
            if (g_ir_status != DEV_IR_OK) break;
            g_ir_last_event = event; g_ir_has_event = true;
            handle_digit(&event);
        }
        if (s_ir->get_overflow_count(s_ir, &overflow) == DEV_IR_OK) g_ir_overflow_count = overflow;
        vTaskDelayUntil(&last, pdMS_TO_TICKS(APP_IR_POLL_MS));
    }
}
/** @brief 初始化红外Device及其固定事件队列，并核对显示、继电器、光耦和蜂鸣器接口。
 * @return 成功APP_OK，红外失败APP_ERROR_IR，显示接口缺失APP_ERROR_DISPLAY，执行器接口缺失APP_ERROR_ACTUATOR。 */
AppStatus App_Ir_Init(void)
{
    s_ir = GetIrRemote();
    s_display = GetDisplay();
    s_actuator = GetActuator();
    if (s_ir == NULL || s_ir->init == NULL || s_ir->read_event == NULL ||
        s_ir->get_overflow_count == NULL) return APP_ERROR_IR;
    if (s_display == NULL || s_display->showFixedPoint == NULL) return APP_ERROR_DISPLAY;
    if (s_actuator == NULL || s_actuator->setRelay == NULL ||
        s_actuator->setOptocoupler == NULL || s_actuator->setBuzzer == NULL) return APP_ERROR_ACTUATOR;
    g_ir_status = s_ir->init(s_ir);
    return g_ir_status == DEV_IR_OK ? APP_OK : APP_ERROR_IR;
}
/** @brief 在启动阶段创建静态蜂鸣定时器和红外任务，定时器由有效按键启动。
 * @return 成功APP_OK，定时器创建失败APP_ERROR_TIMER，任务创建失败APP_ERROR_TASK。 */
AppStatus App_Ir_Start(void)
{
    s_beep_timer = xTimerCreateStatic("ir_beep", pdMS_TO_TICKS(APP_IR_BEEP_MS), pdFALSE,
        NULL, beep_stop_callback, &s_beep_storage);
    if (s_beep_timer == NULL) return APP_ERROR_TIMER;
    s_ir_task = xTaskCreateStatic(ir_task, "ir", APP_IR_TASK_STACK_WORDS, NULL,
        APP_IR_TASK_PRIORITY, s_ir_stack, &s_ir_control);
    return s_ir_task != NULL ? APP_OK : APP_ERROR_TASK;
}
