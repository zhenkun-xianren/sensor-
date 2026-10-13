#include <stddef.h>

#include "FreeRTOS.h"
#include "timers.h"
#include "app_display.h"

#define APP_DISPLAY_SCAN_PERIOD_MS 1U
#define APP_DISPLAY_MIN_PERIOD_TICKS 1U
#define APP_DISPLAY_TIMER_COMMAND_WAIT_TICKS 0U
#define APP_DISPLAY_DECIMAL_PLACES 1U
#define APP_DISPLAY_INITIAL_VALUE 0U

static StaticTimer_t s_scan_timer_storage; // 保存数码管扫描定时器的静态控制块。
static TimerHandle_t s_scan_timer; // 保存每毫秒触发的数码管扫描定时器句柄。
static DevDisplay *s_display; // 保存已初始化的数码管设备接口。
static volatile bool s_display_fault = false; // 锁存扫描或内容提交时的设备访问故障。
static bool s_module_started = false; // 标记设备及扫描定时器是否完整启动。

/**
 * @brief 每毫秒扫描一位数码管，扫描失败后停止继续访问该设备。
 * @param timer 触发本次扫描的周期定时器句柄。
 */
static void display_scan_callback(TimerHandle_t timer)
{
    DevDisplayStatus result; // 保存本次数码管扫描结果。
    (void)timer;
    if (s_display_fault) {
        return;
    }
    result = s_display->scanStep(s_display);
    if (result != DEV_DISPLAY_OK) {
        s_display_fault = true;
    }
}

/**
 * @brief 初始化数码管设备及默认帧，创建并启动一毫秒静态扫描定时器。
 * @return 成功返回APP_DISPLAY_OK，重复启动或资源、设备失败返回APP_DISPLAY_FAILED。
 */
AppDisplayResult AppDisplay_Start(void)
{
    const TickType_t scan_period = pdMS_TO_TICKS(APP_DISPLAY_SCAN_PERIOD_MS); // 数码管扫描间隔对应的Tick数。
    DevDisplayStatus result; // 保存数码管初始化及默认帧提交结果。

    if (s_module_started || s_scan_timer != NULL ||
        scan_period < APP_DISPLAY_MIN_PERIOD_TICKS) {
        return APP_DISPLAY_FAILED;
    }
    s_display = GetDisplay();
    if (s_display == NULL || s_display->init == NULL ||
        s_display->scanStep == NULL || s_display->showFixedPoint == NULL) {
        return APP_DISPLAY_FAILED;
    }
    result = s_display->init(s_display);
    if (result != DEV_DISPLAY_OK) {
        return APP_DISPLAY_FAILED;
    }
    result = s_display->showFixedPoint(
        s_display, APP_DISPLAY_INITIAL_VALUE, APP_DISPLAY_DECIMAL_PLACES,
        DEV_DISPLAY_LEADING_ZERO_PAD);
    if (result != DEV_DISPLAY_OK) {
        return APP_DISPLAY_FAILED;
    }
    s_scan_timer = xTimerCreateStatic("display_scan", scan_period, pdTRUE, NULL,
                                     display_scan_callback, &s_scan_timer_storage);
    if (s_scan_timer == NULL ||
        xTimerStart(s_scan_timer, APP_DISPLAY_TIMER_COMMAND_WAIT_TICKS) != pdPASS) {
        return APP_DISPLAY_FAILED;
    }
    s_module_started = true;
    return APP_DISPLAY_OK;
}

/**
 * @brief 查询显示模块是否完成启动且未锁存设备访问故障。
 * @return 可提交显示内容返回true，未启动或已故障返回false。
 */
bool AppDisplay_IsReady(void)
{
    return s_module_started && !s_display_fault;
}

/**
 * @brief 提交一位小数、整数前导零补齐的显示帧，失败后锁存访问故障。
 * @param scaled_value 放大十倍的整数，0至9999，对应000.0至999.9。
 * @return 成功返回DEV_DISPLAY_OK，未启动或已故障返回对应设备状态，其他失败沿用设备返回值。
 */
DevDisplayStatus AppDisplay_ShowValue(uint16_t scaled_value)
{
    DevDisplayStatus result; // 保存显示帧提交结果。

    if (!s_module_started) {
        return DEV_DISPLAY_NOT_INITIALIZED;
    }
    if (s_display_fault) {
        return DEV_DISPLAY_HARDWARE_ERROR;
    }
    result = s_display->showFixedPoint(
        s_display, scaled_value, APP_DISPLAY_DECIMAL_PLACES,
        DEV_DISPLAY_LEADING_ZERO_PAD);
    if (result != DEV_DISPLAY_OK) {
        s_display_fault = true;
    }
    return result;
}
