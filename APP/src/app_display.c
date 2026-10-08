#include "app_display.h"
#include "app_settings.h"
#include "app_key.h"
#include "dev_display.h"
#include "dev_actuator.h"
#include "dev_gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"
#include <stddef.h>
#define DISPLAY_SCAN_TICKS pdMS_TO_TICKS(1U)
#define DISPLAY_NO_WAIT 0U
#define DISPLAY_MEASUREMENT_CAPACITY 1U
#define DISPLAY_STALE_MS 1500UL
#define DISPLAY_DOUBLE_CLICK_MS 400UL
#define DISPLAY_PAUSE_SCAN_COUNT 3U // 退出设定的实时帧至少完成一轮三位扫描。
#define DISPLAY_MM_PER_TENTH 100U
#define DISPLAY_ROUNDING_MM 50U
#define DISPLAY_DECIMAL_PLACES 1U
#define DISPLAY_RANGE_ERROR 20U
#define DISPLAY_SAVE_ERROR 1U // 保存失败时显示E01。
#define DISPLAY_EDIT_BLINK_ON_MS 500UL // 编辑界面亮相位持续500ms。
#define DISPLAY_EDIT_BLINK_OFF_MS 200UL // 编辑界面灭相位持续200ms。
/** @brief 显示与设定状态；生效值仅在成功保存后切换。 */
typedef enum {
 DISPLAY_LIVE, /**< 检测距离界面。 */
 DISPLAY_EDIT, /**< 设定值编辑界面。 */
 DISPLAY_SAVING, /**< 显示检测值并等待保存结果。 */
 DISPLAY_SAVE_FAILED /**< 保存失败提示界面。 */
} DisplayMode;
/** @brief 最近一次雷达通信结果。 */
typedef struct {
 uint16_t distance_mm; // 原始距离，用于范围及继电器判断。
 bool valid; // 通信完整且status正常。
 uint32_t timestamp_ms; // 该次事务完成时间。
} DisplayMeasurement;
static DevDisplay *s_device; // 唯一扫描源拥有的设备对象。
static DevActuator *s_actuator; // 继电器控制接口。
static StaticTimer_t s_timer_storage; // 1ms扫描定时器控制块。
static TimerHandle_t s_timer; // 唯一扫描源句柄。
static StaticQueue_t s_measurement_storage; // 最新测量队列控制块。
static uint8_t s_measurement_bytes[sizeof(DisplayMeasurement)]; // 单槽测量存储。
static QueueHandle_t s_measurements; // 通信任务到显示状态机的单槽队列。
static DisplayMeasurement s_measurement; // 当前测量及时间。
static DisplayMode s_mode = DISPLAY_LIVE; // 当前界面。
static uint16_t s_active_mm; // 已持久化且当前生效的阈值。
static uint16_t s_draft_mm; // 编辑中的阈值，失败时保留。
static uint32_t s_edit_blink_ms; // 编辑闪烁最近一次相位切换时间。
static bool s_edit_visible = true; // 编辑闪烁亮相位标志，不影响测量与继电器。
static bool s_started; // 是否成功启动。
static bool s_dirty; // 当前显示帧需要更新。
static bool s_paused; // 实际Flash操作期间关闭数码管扫描。
static bool s_current_valid; // 最近有效性判断，用于过期时更新帧。
static bool s_relay_on; // 最近成功提交的线圈驱动状态。
static uint32_t s_pause_scans; // 准备保存时剩余实时帧扫描次数。
static bool s_click_armed; // E01状态下是否观察到新的设定按下。
static bool s_first_click; // 是否等待第二次完整短按。
static uint32_t s_first_click_ms; // 第一次完整短按时间。
static volatile DevDisplayStatus s_last_display_status = DEV_DISPLAY_NOT_INITIALIZED; // 最近显示结果。
static volatile DevStatus s_last_relay_status = DEV_NOT_READY; // 最近继电器请求结果。
/** @brief 根据当前时间核对测量是否有效。 @param now_ms 当前毫秒。 @return 通信正常且未过期返回true。 */
static bool display_measurement_valid(uint32_t now_ms)
{
 return s_measurement.valid && (uint32_t)(now_ms - s_measurement.timestamp_ms) <= DISPLAY_STALE_MS;
}
/** @brief 提交当前界面整帧，编辑熄灭相位提交全空白，不把舍入结果用于范围判断。 @param now_ms 当前毫秒。 @return 显示设备结果。 */
static DevDisplayStatus display_render(uint32_t now_ms)
{
 uint16_t distance; // 本次显示的原始毫米数。
 if (s_mode == DISPLAY_SAVE_FAILED) return s_device->showError(s_device, DISPLAY_SAVE_ERROR);
 if (s_mode == DISPLAY_EDIT) {
     if (!s_edit_visible) return s_device->clear(s_device);
     distance = s_draft_mm;
 }
 else {
     if (!display_measurement_valid(now_ms)) return s_device->showDashes(s_device);
     if (s_measurement.distance_mm > APP_SETTINGS_MAX_MM)
         return s_device->showError(s_device, DISPLAY_RANGE_ERROR);
     distance = s_measurement.distance_mm;
 }
 return s_device->showFixedPoint(s_device,
     (uint16_t)((distance + DISPLAY_ROUNDING_MM) / DISPLAY_MM_PER_TENTH),
     DISPLAY_DECIMAL_PLACES, DEV_DISPLAY_LEADING_ZERO_SUPPRESS);
}
/** @brief 有效原始距离严格小于生效阈值时请求线圈吸合，等于或大于时释放。 @param now_ms 当前毫秒。 */
static void display_update_relay(uint32_t now_ms)
{
 bool enabled = display_measurement_valid(now_ms) &&
     s_measurement.distance_mm <= APP_SETTINGS_MAX_MM && s_measurement.distance_mm < s_active_mm; // 当前驱动请求。
 if (enabled == s_relay_on && s_last_relay_status == DEV_OK) return;
 s_last_relay_status = s_actuator->setRelay(s_actuator, enabled);
 if (s_last_relay_status == DEV_OK) s_relay_on = enabled;
 else {
     (void)s_actuator->setRelay(s_actuator, false);
     s_relay_on = false;
 }
}
/** @brief 清除未完成双击，忽略触发保存那次按下的残留释放。 */
void App_Display_ResetClicks(void)
{
 s_click_armed = false;
 s_first_click = false;
}
/** @brief 消费测量和保存阶段，按亮500ms、灭200ms切换编辑闪烁并扫描一位，不执行Flash或等待总线。 @param timer 扫描定时器句柄。 */
static void display_timer_callback(TimerHandle_t timer)
{
 uint32_t now_ms = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS; // 当前调度时间。
 DisplayMeasurement measurement; // 最新通信结果。
 AppSettingsEvent event; // 保存交接消息。
 bool valid; // 当前测量有效性。
 (void)timer;
 if (xQueueReceive(s_measurements, &measurement, DISPLAY_NO_WAIT) == pdPASS) {
     s_measurement = measurement;
     s_dirty = true;
 }
 valid = display_measurement_valid(now_ms);
 if (valid != s_current_valid) { s_current_valid = valid; s_dirty = true; }
 while (App_Settings_TakeEvent(&event)) {
     if (event.type == APP_SETTINGS_PREPARE) s_pause_scans = DISPLAY_PAUSE_SCAN_COUNT;
     else {
         s_paused = false;
         s_pause_scans = 0U;
         if (event.type == APP_SETTINGS_SAVED) {
             s_active_mm = event.threshold_mm;
             s_mode = DISPLAY_LIVE;
         } else s_mode = DISPLAY_SAVE_FAILED;
         App_Display_ResetClicks();
         s_dirty = true;
     }
 }
 if (s_mode == DISPLAY_EDIT &&
     (uint32_t)(now_ms - s_edit_blink_ms) >=
         (s_edit_visible ? DISPLAY_EDIT_BLINK_ON_MS : DISPLAY_EDIT_BLINK_OFF_MS)) {
     s_edit_visible = !s_edit_visible;
     s_edit_blink_ms = now_ms;
     s_dirty = true;
 }
 display_update_relay(now_ms);
 if (s_paused) return;
 if (s_dirty) {
     s_last_display_status = display_render(now_ms);
     s_dirty = false;
 }
 if (s_last_display_status == DEV_DISPLAY_OK) s_last_display_status = s_device->scanStep(s_device);
 if (s_pause_scans != 0U && --s_pause_scans == 0U) {
     s_last_display_status = s_device->stop(s_device);
     s_paused = true;
     App_Settings_AllowSave(s_last_display_status == DEV_DISPLAY_OK);
 }
}
/** @brief 初始化继电器、等待帧和测量队列，启动唯一周期扫描。 @return 启动结果。 */
AppDisplayResult App_Display_Start(void)
{
 DevGpio *gpio; // 执行器GPIO初始化接口。
 if (s_started) return APP_DISPLAY_OK;
 gpio = GetGpio();
 s_actuator = GetActuator();
 s_device = GetDisplay();
 if (gpio == NULL || gpio->init == NULL || s_actuator == NULL ||
     s_actuator->init == NULL || s_actuator->setRelay == NULL) return APP_DISPLAY_RELAY_ERROR;
 s_last_relay_status = gpio->init(gpio);
 if (s_last_relay_status == DEV_OK) s_last_relay_status = s_actuator->init(s_actuator);
 if (s_last_relay_status == DEV_OK) s_last_relay_status = s_actuator->setRelay(s_actuator, false);
 if (s_last_relay_status != DEV_OK) return APP_DISPLAY_RELAY_ERROR;
 if (s_device == NULL || s_device->init == NULL || s_device->scanStep == NULL ||
     s_device->showFixedPoint == NULL || s_device->showError == NULL ||
     s_device->showDashes == NULL || s_device->clear == NULL ||
     s_device->stop == NULL) return APP_DISPLAY_DEVICE_ERROR;
 s_active_mm = App_Settings_InitialValue();
 s_draft_mm = s_active_mm;
 s_last_display_status = s_device->init(s_device);
 if (s_last_display_status == DEV_DISPLAY_OK) s_last_display_status = s_device->showDashes(s_device);
 if (s_last_display_status != DEV_DISPLAY_OK) return APP_DISPLAY_DEVICE_ERROR;
 s_measurements = xQueueCreateStatic(DISPLAY_MEASUREMENT_CAPACITY, sizeof(DisplayMeasurement),
     s_measurement_bytes, &s_measurement_storage);
 s_timer = xTimerCreateStatic("display_scan", DISPLAY_SCAN_TICKS, pdTRUE,
     NULL, display_timer_callback, &s_timer_storage);
 if (s_measurements == NULL || s_timer == NULL || xTimerStart(s_timer, DISPLAY_NO_WAIT) != pdPASS) {
     (void)s_device->stop(s_device);
     return APP_DISPLAY_TIMER_ERROR;
 }
 s_started = true;
 return APP_DISPLAY_OK;
}
/** @brief 设定键切换闪烁编辑并按配置步进调整；E01仅双击完整短按返回编辑且从亮相位开始。 @param event 已消抖按键事件。 */
void App_Display_ProcessKey(const DevKeyEvent *event)
{
 uint32_t now_ms; // 实时帧提交时刻。
 if (!s_started || event == NULL || s_mode == DISPLAY_SAVING) return;
 if (s_mode == DISPLAY_SAVE_FAILED) {
     if (event->id != APP_KEY_SET) return;
     if (event->type == DEV_KEY_EVENT_PRESS) s_click_armed = true;
     else if (event->type == DEV_KEY_EVENT_LONG) App_Display_ResetClicks();
     else if (event->type == DEV_KEY_EVENT_SHORT && s_click_armed) {
         s_click_armed = false;
         if (s_first_click && (uint32_t)(event->timestamp_ms - s_first_click_ms) <= DISPLAY_DOUBLE_CLICK_MS) {
             s_mode = DISPLAY_EDIT;
             s_edit_visible = true;
             s_edit_blink_ms = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
             App_Display_ResetClicks();
             s_dirty = true;
         } else { s_first_click = true; s_first_click_ms = event->timestamp_ms; }
     }
     return;
 }
 if (event->type != DEV_KEY_EVENT_PRESS) return;
 if (event->id == APP_KEY_SET) {
     if (s_mode == DISPLAY_LIVE) {
         s_draft_mm = s_active_mm;
         s_mode = DISPLAY_EDIT;
         s_edit_visible = true;
         s_edit_blink_ms = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
         s_dirty = true;
     } else {
         /* 同一按下事件先提交检测帧，再排队保存，从此禁止加减。 */
         s_mode = DISPLAY_SAVING;
         now_ms = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
         s_last_display_status = display_render(now_ms);
         s_dirty = false;
         App_Display_ResetClicks();
         if (s_last_display_status != DEV_DISPLAY_OK || !App_Settings_RequestSave(s_draft_mm)) {
             s_mode = DISPLAY_SAVE_FAILED;
             s_dirty = true;
         }
     }
 } else if (s_mode == DISPLAY_EDIT) {
     if (event->id == APP_KEY_INCREASE && s_draft_mm <= APP_SETTINGS_MAX_MM - APP_SETTINGS_STEP_MM)
         s_draft_mm = (uint16_t)(s_draft_mm + APP_SETTINGS_STEP_MM);
     else if (event->id == APP_KEY_DECREASE && s_draft_mm >= APP_SETTINGS_STEP_MM)
         s_draft_mm = (uint16_t)(s_draft_mm - APP_SETTINGS_STEP_MM);
     s_dirty = true;
 }
}
/** @brief 发布事务完成后的最新原始测量。 @param distance_mm 原始毫米距离。 @param valid 应答及设备状态是否正常。 */
void App_Display_PublishDistance(uint16_t distance_mm, bool valid)
{
 DisplayMeasurement measurement; // 本次测量消息。
 measurement.distance_mm = distance_mm;
 measurement.valid = valid;
 measurement.timestamp_ms = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
 if (s_measurements != NULL) configASSERT(xQueueOverwrite(s_measurements, &measurement) == pdPASS);
}
