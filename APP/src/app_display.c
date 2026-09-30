#include "app_display.h"
#include "dev_display.h"
#include "FreeRTOS.h"
#include "timers.h"
#include <stddef.h>

#define APP_DISPLAY_SCAN_PERIOD_MS 5U
#define APP_DISPLAY_ERROR_CODE 90U
#define APP_DISPLAY_TIMER_COMMAND_WAIT_TICKS 0U

static StaticTimer_t s_display_timer_buffer;
static TimerHandle_t s_display_timer;
static DevDisplay *s_display;

/**
 * @brief 根据应用状态选择当前显示内容的优先级。
 * @param init_fault 初始化失败时为非零。
 * @param diagnostic 启动自检仍在进行时为非零。
 * @param remote_visible 需要显示遥控数字时为非零。
 * @param sensor_error 最近一次传感器请求失败时为非零。
 * @return 按优先级选出的显示状态。
 */
AppDisplayChoice AppDisplay_Choose(uint8_t init_fault, uint8_t diagnostic,
                                    uint8_t remote_visible, uint8_t sensor_error)
{
    if (init_fault != 0U) return APP_DISPLAY_INIT_FAULT;
    if (diagnostic != 0U) return APP_DISPLAY_DIAGNOSTIC;
    if (remote_visible != 0U) return APP_DISPLAY_REMOTE;
    if (sensor_error != 0U) return APP_DISPLAY_SENSOR_ERROR;
    return APP_DISPLAY_CONCENTRATION;
}

/**
 * @brief 执行一次数码管逐位扫描。
 * @param timer 触发本次回调的软件定时器句柄，本回调不使用。
 */
static void display_scan_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    if (s_display != NULL) {
        s_display->scanStep(s_display);
    }
}

/**
 * @brief 初始化数码管并创建负责逐位扫描的软件定时器。
 * @return 数码管和软件定时器初始化成功返回 1；否则返回 0。
 */
uint8_t AppDisplay_Init(void)
{
    s_display = GetDisplay();
    if (s_display == NULL) {
        return 0U;
    }
    s_display->init(s_display);
    s_display->showInteger4(s_display, 0U);
    s_display_timer = xTimerCreateStatic("disp_scan",
                                         pdMS_TO_TICKS(APP_DISPLAY_SCAN_PERIOD_MS),
                                         pdTRUE, NULL,
                                         display_scan_timer_callback,
                                         &s_display_timer_buffer);
    if (s_display_timer == NULL ||
        xTimerStart(s_display_timer,
                    APP_DISPLAY_TIMER_COMMAND_WAIT_TICKS) != pdPASS) {
        s_display->showError(s_display, APP_DISPLAY_ERROR_CODE);
        return 0U;
    }
    return 1U;
}
