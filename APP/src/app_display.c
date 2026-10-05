#include "app_display.h"
#include "dev_oled.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"
#include <stddef.h>
#define APP_DISPLAY_PERIOD_MS 1000U
#define APP_DISPLAY_TASK_STACK_WORDS 256U
#define APP_DISPLAY_TASK_PRIORITY 2U
#define APP_DISPLAY_TIMER_COMMAND_WAIT 0U
#define APP_DISPLAY_NOTIFY_TICK (1UL << 0U)
#define APP_DISPLAY_NOTIFY_KEY (1UL << 1U)
#define APP_DISPLAY_NOTIFY_ALL (APP_DISPLAY_NOTIFY_TICK | APP_DISPLAY_NOTIFY_KEY)
#define APP_DISPLAY_KEY_LINE 1U
static StaticTask_t s_display_task_tcb; // 唯一显示任务的静态控制块。
static StackType_t s_display_task_stack[APP_DISPLAY_TASK_STACK_WORDS]; // 显示任务静态栈，单位为栈字。
static TaskHandle_t s_display_task_handle; // 接收计时与按键通知的显示任务句柄。
static StaticTimer_t s_display_timer_tcb; // 一秒软件定时器的静态控制块。
static TimerHandle_t s_display_timer; // 一秒软件定时器句柄。
static StaticQueue_t s_key_queue_tcb; // 最新按键文字邮箱的静态控制块。
static uint8_t s_key_queue_storage[APP_DISPLAY_TEXT_CAPACITY]; // 单元素邮箱的固定文字存储。
static QueueHandle_t s_key_queue; // 始终保留最新待显示按键的邮箱。
volatile AppDisplayStatus g_app_display_status; // OLED 初始化与运行状态。
volatile AppDisplayError g_app_display_error; // OLED 首次失败阶段。
volatile uint32_t g_app_display_counter; // 最近成功发送的首行计数。
/**
 * @brief 记录显示失败阶段并进入断言故障处理。
 * @param error 当前失败的业务阶段。
 */
static void display_fail(AppDisplayError error)
{
    g_app_display_error = error;
    g_app_display_status = APP_DISPLAY_ERROR;
    configASSERT(0);
    for (;;) { vTaskDelay(portMAX_DELAY); }
}
/**
 * @brief 通知显示任务处理计时事件，回调不访问 SPI。
 * @param timer 触发回调的软件定时器句柄。
 */
static void display_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    if (s_display_task_handle != NULL) {
        (void)xTaskNotify(s_display_task_handle, APP_DISPLAY_NOTIFY_TICK, eSetBits);
    }
}
/**
 * @brief 投递最新按键文字，较旧的待显示文字可被覆盖。
 * @param text 最多21个可打印ASCII字符的零结尾文本。
 * @return 投递成功返回1，参数无效或资源未就绪返回0。
 */
uint8_t AppDisplay_SetKeyText(const char *text)
{
    char message[APP_DISPLAY_TEXT_CAPACITY] = {0}; // 投递到静态邮箱的完整定长文字。
    uint8_t length = 0U; // 已检查和复制的字符数。
    if (text == NULL || s_key_queue == NULL || s_display_task_handle == NULL) return 0U;
    while (text[length] != '\0') {
        if (length >= APP_DISPLAY_TEXT_CAPACITY - 1U || (uint8_t)text[length] < 32U ||
            (uint8_t)text[length] > 126U) return 0U;
        message[length] = text[length];
        ++length;
    }
    if (xQueueOverwrite(s_key_queue, message) != pdPASS) return 0U;
    return xTaskNotify(s_display_task_handle, APP_DISPLAY_NOTIFY_KEY, eSetBits) == pdPASS ? 1U : 0U;
}
/**
 * @brief 作为唯一 OLED 写入者，独立处理首行计时和第二行按键。
 * @param context 保留任务参数，本任务不使用。
 */
static void display_task(void *context)
{
    DevOled *oled = GetOled(); // 与板级无关的 OLED 设备接口。
    uint32_t counter = 0U; // 首行显示的逐秒计数。
    uint32_t notifications; // 本次收到的计时和按键事件位。
    TickType_t last_tick; // 最近已计入首行计数的时间边界。
    TickType_t elapsed; // 当前距上一计数边界的 Tick 差，允许自然回绕。
    TickType_t seconds; // 本次应补计的整秒数。
    const TickType_t period = pdMS_TO_TICKS(APP_DISPLAY_PERIOD_MS); // 一秒对应的系统 Tick 数。
    char key_text[APP_DISPLAY_TEXT_CAPACITY]; // 从最新文字邮箱取出的完整消息。
    (void)context;
    g_app_display_status = APP_DISPLAY_INITIALIZING;
    if (oled == NULL || oled->init == NULL || oled->showCounter == NULL || oled->writeLine == NULL) {
        display_fail(APP_DISPLAY_ERROR_DEVICE);
    }
    if (oled->init(oled) == 0U) display_fail(APP_DISPLAY_ERROR_INIT);
    if (oled->showCounter(oled, counter) == 0U) display_fail(APP_DISPLAY_ERROR_FIRST_FRAME);
    if (oled->writeLine(oled, APP_DISPLAY_KEY_LINE, "Key: --") == 0U) {
        display_fail(APP_DISPLAY_ERROR_KEY_LINE);
    }
    last_tick = xTaskGetTickCount();
    if (xTimerStart(s_display_timer, APP_DISPLAY_TIMER_COMMAND_WAIT) != pdPASS) {
        display_fail(APP_DISPLAY_ERROR_TIMER);
    }
    g_app_display_status = APP_DISPLAY_RUNNING;
    for (;;) {
        (void)xTaskNotifyWait(0U, APP_DISPLAY_NOTIFY_ALL, &notifications, portMAX_DELAY);
        if ((notifications & APP_DISPLAY_NOTIFY_TICK) != 0U) {
            /* 按经过的 Tick 补计整秒，通知合并不丢秒；按键通知不推进数字。 */
            elapsed = (TickType_t)(xTaskGetTickCount() - last_tick);
            seconds = elapsed / period;
            if (seconds != 0U) {
                counter += seconds;
                last_tick += seconds * period;
                if (oled->showCounter(oled, counter) == 0U) display_fail(APP_DISPLAY_ERROR_REFRESH);
                g_app_display_counter = counter;
            }
        }
        if ((notifications & APP_DISPLAY_NOTIFY_KEY) != 0U &&
            xQueueReceive(s_key_queue, key_text, 0U) == pdPASS) {
            if (oled->writeLine(oled, APP_DISPLAY_KEY_LINE, key_text) == 0U) {
                display_fail(APP_DISPLAY_ERROR_KEY_LINE);
            }
        }
    }
}
/**
 * @brief 创建静态显示邮箱、唯一显示任务和一秒定时器。
 * @return 所有静态资源创建成功返回1，否则返回0。
 */
uint8_t AppDisplay_Start(void)
{
    TickType_t period = pdMS_TO_TICKS(APP_DISPLAY_PERIOD_MS); // 一秒刷新周期的 Tick 数。
    g_app_display_status = APP_DISPLAY_PENDING;
    g_app_display_error = APP_DISPLAY_ERROR_NONE;
    g_app_display_counter = 0U;
    if (period == 0U) {
        g_app_display_error = APP_DISPLAY_ERROR_RESOURCE;
        g_app_display_status = APP_DISPLAY_ERROR;
        return 0U;
    }
    s_key_queue = xQueueCreateStatic(1U, APP_DISPLAY_TEXT_CAPACITY,
                                    s_key_queue_storage, &s_key_queue_tcb);
    s_display_timer = xTimerCreateStatic("oled_1s", period, pdTRUE, NULL,
                                         display_timer_callback, &s_display_timer_tcb);
    if (s_key_queue != NULL && s_display_timer != NULL) {
        s_display_task_handle = xTaskCreateStatic(display_task, "oled",
            APP_DISPLAY_TASK_STACK_WORDS, NULL, APP_DISPLAY_TASK_PRIORITY,
            s_display_task_stack, &s_display_task_tcb);
    }
    if (s_key_queue == NULL || s_display_timer == NULL || s_display_task_handle == NULL) {
        g_app_display_error = APP_DISPLAY_ERROR_RESOURCE;
        g_app_display_status = APP_DISPLAY_ERROR;
        return 0U;
    }
    return 1U;
}
