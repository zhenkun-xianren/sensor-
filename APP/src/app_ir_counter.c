#include "app_ir_counter.h"
#include "dev_display.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>

#define APP_IR_COUNTER_POLL_PERIOD_MS 10U
#define APP_IR_COUNTER_TASK_STACK_WORDS 192U
#define APP_IR_COUNTER_TASK_PRIORITY 2U

static StaticTask_t s_ir_counter_task_tcb;
static StackType_t s_ir_counter_task_stack[APP_IR_COUNTER_TASK_STACK_WORDS];
static DevDisplay *s_ir_counter_display;

/**
 * @brief 从有效红外数字事件中提取待显示数字。
 * @param event 已解码的红外遥控事件。
 * @param digit 用于接收数字键值的输出指针，范围为 0 至 9。
 * @return event 和 digit 有效且事件为非重复数字键时返回 1，否则返回 0。
 */
uint8_t AppIrCounter_GetDisplayDigit(const IrKeyEvent *event, uint8_t *digit)
{
    if (event == NULL || digit == NULL || event->repeat != 0U ||
        event->digit < 0 || event->digit > 9) {
        return 0U;
    }

    *digit = (uint8_t)event->digit;
    return 1U;
}

/**
 * @brief 轮询已解码的红外按键事件并显示对应数字。
 * @param argument FreeRTOS 任务参数，本任务未使用。
 */
static void ir_counter_task(void *argument)
{
    IrKeyEvent event;
    uint8_t digit;

    (void)argument;
    for (;;) {
        while (DevIrRemote_ReadEvent(&event) != 0U) {
            if (AppIrCounter_GetDisplayDigit(&event, &digit) != 0U &&
                s_ir_counter_display != NULL) {
                s_ir_counter_display->clear(s_ir_counter_display);
                s_ir_counter_display->showDigit(s_ir_counter_display,
                                                DEV_DISPLAY_DIG4, digit,
                                                DEV_DISPLAY_DECIMAL_POINT_OFF);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(APP_IR_COUNTER_POLL_PERIOD_MS));
    }
}

/**
 * @brief 初始化红外接收和四位零值显示，并创建静态按键显示任务。
 * @return 初始化及任务创建成功返回 1；否则返回 0。
 */
uint8_t AppIrCounter_Init(void)
{
    TaskHandle_t task_handle;

    s_ir_counter_display = GetDisplay();
    if (s_ir_counter_display == NULL) {
        return 0U;
    }

    s_ir_counter_display->showInteger4(s_ir_counter_display,
                                       0U);
    DevIrRemote_Init();

    task_handle = xTaskCreateStatic(ir_counter_task, "ir_count",
                                   APP_IR_COUNTER_TASK_STACK_WORDS, NULL,
                                   APP_IR_COUNTER_TASK_PRIORITY,
                                   s_ir_counter_task_stack,
                                   &s_ir_counter_task_tcb);
    return task_handle != NULL ? 1U : 0U;
}
