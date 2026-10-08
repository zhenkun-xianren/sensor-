#include "app_key.h"
#include "app_display.h"
#include "dev_key.h"
#include "dev_key_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <stdbool.h>
#include <stddef.h>

/* 以整数Tick表达周期，防止后续改配置产生零周期或隐式截断。 */
#define APP_KEY_MILLISECONDS_PER_SECOND 1000UL
#define APP_KEY_PERIOD_TICKS ((DEV_KEY_SCAN_PERIOD_MS * configTICK_RATE_HZ) / APP_KEY_MILLISECONDS_PER_SECOND)
#define APP_KEY_TIMER_NO_WAIT 0U
#define APP_KEY_EVENT_READ_LIMIT (DEV_KEY_EVENT_CAPACITY + 1U)
#if (DEV_KEY_SCAN_PERIOD_MS * configTICK_RATE_HZ) % APP_KEY_MILLISECONDS_PER_SECOND != 0 || APP_KEY_PERIOD_TICKS == 0
#error Key_scan_period_must_be_a_positive_integer_tick_count
#endif

static DevKey *s_device; // 周期扫描使用的Device对象，不访问Port或BSP。
static StaticTimer_t s_timer_storage; // 扫描定时器的静态控制块。
static TimerHandle_t s_timer; // 唯一周期扫描定时器句柄。
static bool s_started = false; // 模块是否已成功提交启动命令。
static volatile DevKeyResult s_last_scan_result = DEV_KEY_NOT_INITIALIZED; // 调试查看最近扫描结果。
static volatile uint32_t s_overflow_scan_count; // 曾发生事件丢失的扫描次数。
static volatile DevKeyResult s_last_read_result = DEV_KEY_NO_EVENT; // 保存事件消费结果，区别于扫描结果。

/**
 * @brief 有界消费按键事件，转交显示状态机区分单按与失败提示的双击。
 */
static void app_key_process_events(void)
{
    DevKeyEvent event; // 保存当前读取到的按键事件。
    uint32_t read_count; // 限制单次回调的队列读取次数，包含一次溢出提示。
    for (read_count = 0U; read_count < APP_KEY_EVENT_READ_LIMIT; ++read_count) {
        s_last_read_result = s_device->read(s_device, &event);
        if (s_last_read_result == DEV_KEY_OVERFLOW) {
            App_Display_ResetClicks();
            continue;
        }
        if (s_last_read_result != DEV_KEY_OK) break;
        App_Display_ProcessKey(&event);
    }
}

/**
 * @brief 执行非阻塞设备扫描并消费事件，由显示APP状态机决定设定及双击动作。
 * @param timer 当前静态扫描定时器，不在回调中阻塞或分配内存。
 */
static void app_key_scan_callback(TimerHandle_t timer)
{
    uint32_t now_ms = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS; // 获取单调毫秒时间。
    (void)timer;
    s_last_scan_result = s_device->scan(s_device, now_ms);
    if (s_last_scan_result == DEV_KEY_OVERFLOW) ++s_overflow_scan_count;
    if (s_last_scan_result == DEV_KEY_OK || s_last_scan_result == DEV_KEY_OVERFLOW)
        app_key_process_events();
}

/**
 * @brief 初始化三键设备并创建、启动唯一的静态周期扫描定时器。
 * @return 成功返回APP_KEY_OK，失败返回设备或定时器错误。
 */
AppKeyResult App_Key_Start(void)
{
    DevKeyResult result; // 设备初始化结果。
    if (s_started) return APP_KEY_OK;
    s_device = GetKey();
    if (s_device == NULL || s_device->init == NULL || s_device->scan == NULL ||
        s_device->read == NULL || s_device->get_state == NULL)
        return APP_KEY_DEVICE_ERROR;
    result = s_device->init(s_device);
    if (result != DEV_KEY_OK) {
        s_last_scan_result = result;
        return APP_KEY_DEVICE_ERROR;
    }
    if (s_timer == NULL) {
        s_timer = xTimerCreateStatic("key_scan", APP_KEY_PERIOD_TICKS, pdTRUE,
            NULL, app_key_scan_callback, &s_timer_storage);
    }
    if (s_timer == NULL || xTimerStart(s_timer, APP_KEY_TIMER_NO_WAIT) != pdPASS)
        return APP_KEY_TIMER_ERROR;
    s_last_scan_result = DEV_KEY_OK;
    s_started = true;
    return APP_KEY_OK;
}
