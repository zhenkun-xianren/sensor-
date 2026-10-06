#include "app_display.h"
#include "app_measurement.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <stddef.h>
#define APP_DISPLAY_SCAN_MS 1U
#define APP_DISPLAY_COMMAND_WAIT_TICKS 0U
#define APP_DISPLAY_VOLTAGE_DECIMAL_PLACES 3U /* 毫伏按三位小数显示为伏。 */
#define APP_DISPLAY_ERROR_WAITING 1U /* E01：未就绪或数据过期。 */
#define APP_DISPLAY_ERROR_ADC 2U /* E02：ADC初始化或转换失败。 */
#define APP_DISPLAY_ERROR_REFERENCE 3U /* E03：参考为零或饱和。 */
#define APP_DISPLAY_ERROR_RANGE 4U /* E04：信号饱和或输入越界。 */
static DevDisplay *s_display; // 保存唯一显示Device对象。
static StaticTimer_t s_scan_storage; // 保存静态软件定时器控制块。
static TimerHandle_t s_scan_timer; // 保存扫描定时器句柄。
static TickType_t s_content_tick; // 保存最近一次测量显示内容更新的Tick。
volatile DevDisplayStatus g_display_scan_status = DEV_DISPLAY_NOT_INITIALIZED; // 记录最近扫描返回状态。
volatile DevDisplayStatus g_display_content_status = DEV_DISPLAY_NOT_INITIALIZED; // 保存显示内容提交结果。
/**
 * @brief 从测量快照提交电压或错误帧，不启动ADC或等待硬件。
 */
static void display_update_measurement(void)
{
    AppMeasurementSnapshot snapshot; // 保存供本次显示使用的完整测量快照。
    AppMeasurementStatus status; // 保存数据新鲜度及有效性检查结果。
    uint8_t error_code = APP_DISPLAY_ERROR_ADC; // 保存无效测量对应的数码管错误码。
    status = App_Measurement_GetLatest(&snapshot);
    if (status == APP_MEASUREMENT_OK) {
        g_display_content_status = s_display->showFixedPoint(s_display, snapshot.millivolts,
            APP_DISPLAY_VOLTAGE_DECIMAL_PLACES, DEV_DISPLAY_LEADING_ZERO_PAD);
        return;
    }
    switch (status) {
    case APP_MEASUREMENT_NOT_READY:
    case APP_MEASUREMENT_STALE:
        error_code = APP_DISPLAY_ERROR_WAITING;
        break;
    case APP_MEASUREMENT_REFERENCE_ERROR:
        error_code = APP_DISPLAY_ERROR_REFERENCE;
        break;
    case APP_MEASUREMENT_OUT_OF_RANGE:
        error_code = APP_DISPLAY_ERROR_RANGE;
        break;
    default:
        break;
    }
    g_display_content_status = s_display->showError(s_display, error_code);
}
/** @brief 每毫秒执行一次扫描；每100ms更新一次测量帧，不进行ADC或串口等待。
 * @param timer 本模块静态定时器句柄。 */
static void display_scan_callback(TimerHandle_t timer)
{
    TickType_t now_tick = xTaskGetTickCount(); // 保存本次扫描的调度Tick。
    (void)timer;
    if ((TickType_t)(now_tick - s_content_tick) >= pdMS_TO_TICKS(APP_MEASUREMENT_PERIOD_MS)) {
        display_update_measurement();
        s_content_tick = now_tick;
    }
    g_display_scan_status = s_display->scanStep(s_display);
}
/** @brief 初始化显示Device，首次有效测量前提交E01等待帧。
 * @return 成功APP_OK，否则APP_ERROR_DISPLAY。 */
AppStatus App_Display_Init(void)
{
    s_display = GetDisplay();
    if (s_display == NULL || s_display->init(s_display) != DEV_DISPLAY_OK ||
        s_display->showError(s_display, APP_DISPLAY_ERROR_WAITING) != DEV_DISPLAY_OK) return APP_ERROR_DISPLAY;
    g_display_content_status = DEV_DISPLAY_OK;
    g_display_scan_status = DEV_DISPLAY_OK; return APP_OK;
}
/** @brief 创建1ms静态扫描定时器并检查启动命令是否入队。
 * @return 成功APP_OK，否则APP_ERROR_TIMER。 */
AppStatus App_Display_Start(void)
{
    s_content_tick = xTaskGetTickCount();
    s_scan_timer = xTimerCreateStatic("display", pdMS_TO_TICKS(APP_DISPLAY_SCAN_MS), pdTRUE,
        NULL, display_scan_callback, &s_scan_storage);
    if (s_scan_timer == NULL || xTimerStart(s_scan_timer, APP_DISPLAY_COMMAND_WAIT_TICKS) != pdPASS) return APP_ERROR_TIMER;
    return APP_OK;
}
