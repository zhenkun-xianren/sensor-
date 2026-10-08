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
/** @brief 雷达测量状态；错误值对应数码管E码，等待状态仅显示横线。 */
typedef enum {
 APP_RADAR_OK = 0U, /**< 完整应答且雷达状态正常。 */
 APP_RADAR_TIMEOUT = 2U, /**< E02：本地发送或等待应答超时。 */
 APP_RADAR_CRC_ERROR = 3U, /**< E03：应答CRC校验失败。 */
 APP_RADAR_RESPONSE_ERROR = 5U, /**< E05：应答地址、功能码或长度不匹配。 */
 APP_RADAR_EXCEPTION = 6U, /**< E06：雷达返回Modbus异常应答。 */
 APP_RADAR_IO_ERROR = 7U, /**< E07：串口操作、板级配置或接收缓冲区错误。 */
 APP_RADAR_STATUS_ERROR = 8U, /**< E08：通信成功但雷达工作状态不正常。 */
 APP_RADAR_STALE = 9U, /**< E09：超过1.5秒没有新的查询结果。 */
 APP_RADAR_ARGUMENT_ERROR = 10U, /**< E10：通信调用参数无效。 */
 APP_RADAR_NOT_INITIALIZED = 11U, /**< E11：通信设备尚未初始化。 */
 APP_RADAR_WAITING = 255U /**< 开机等待首次查询结果，不编码为E码。 */
} AppRadarStatus;
/** @brief 恢复设定值，显示等待符并启动唯一1ms扫描源。 @return 启动结果。 */
AppDisplayResult App_Display_Start(void);
/** @brief 在定时器任务中处理按键事件。 @param event 已消抖的完整事件。 */
void App_Display_ProcessKey(const DevKeyEvent *event);
/** @brief 按键事件丢失后清除双击识别状态。 */
void App_Display_ResetClicks(void);
/**
 * @brief 通信任务发布最新测量及具体状态，使用单槽静态队列。
 * @param distance_mm 原始毫米值，仅APP_RADAR_OK时用于距离和继电器判断。
 * @param status 本次查询的明确状态，错误时立即使旧距离失效。
 */
void App_Display_PublishDistance(uint16_t distance_mm, AppRadarStatus status);
#endif
