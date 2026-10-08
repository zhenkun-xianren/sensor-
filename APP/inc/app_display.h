#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H
#include "dev_key.h"
#include <stdbool.h>
#include <stdint.h>
/** @brief 显示应用启动结果。 */
typedef enum {
 APP_DISPLAY_OK = 0, /**< 显示和继电器就绪。 */
 APP_DISPLAY_DEVICE_ERROR = -1, /**< 显示设备失败。 */
 APP_DISPLAY_TIMER_ERROR = -2, /**< 扫描定时器或队列失败。 */
 APP_DISPLAY_NOT_STARTED = -3, /**< 尚未启动。 */
 APP_DISPLAY_RELAY_ERROR = -5 /**< 继电器设备失败。 */
} AppDisplayResult;
/** @brief 恢复设定值，显示等待符并启动唯一1ms扫描源。 @return 启动结果。 */
AppDisplayResult App_Display_Start(void);
/** @brief 在定时器任务中处理按键事件。 @param event 已消抖的完整事件。 */
void App_Display_ProcessKey(const DevKeyEvent *event);
/** @brief 按键事件丢失后清除双击识别状态。 */
void App_Display_ResetClicks(void);
/** @brief 通信任务发布最新测量，使用单槽静态队列。 @param distance_mm 原始毫米值。 @param valid 完整应答且状态正常。 */
void App_Display_PublishDistance(uint16_t distance_mm, bool valid);
#endif
