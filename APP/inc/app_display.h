#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include <stdint.h>

#define APP_DISPLAY_TEXT_CAPACITY 22U

/** @brief OLED 演示任务的启动与运行状态。 */
typedef enum {
    APP_DISPLAY_PENDING = 0, /**< 静态资源尚未全部创建。 */
    APP_DISPLAY_INITIALIZING, /**< 正在初始化 OLED 硬件。 */
    APP_DISPLAY_RUNNING, /**< 已显示首个数字并开始逐秒更新。 */
    APP_DISPLAY_ERROR /**< OLED 初始化或通信失败。 */
} AppDisplayStatus;

/** @brief OLED 显示业务的首次失败阶段。 */
typedef enum {
    APP_DISPLAY_ERROR_NONE = 0, /**< 未发生错误。 */
    APP_DISPLAY_ERROR_RESOURCE, /**< 任务或定时器资源创建失败。 */
    APP_DISPLAY_ERROR_DEVICE, /**< 设备接口无效。 */
    APP_DISPLAY_ERROR_INIT, /**< 设备初始化失败。 */
    APP_DISPLAY_ERROR_FIRST_FRAME, /**< 初始文字发送失败。 */
    APP_DISPLAY_ERROR_TIMER, /**< 定时器启动失败。 */
    APP_DISPLAY_ERROR_REFRESH, /**< 运行中的文字更新失败。 */
    APP_DISPLAY_ERROR_KEY_LINE /**< 第二行文字发送失败。 */
} AppDisplayError;

extern volatile AppDisplayStatus g_app_display_status; // 供 SWD 检查的显示任务状态。
extern volatile AppDisplayError g_app_display_error; // 供 SWD 检查的失败阶段。
extern volatile uint32_t g_app_display_counter; // 最近成功发送的数字；不代表屏幕实际显示成功。

/**
 * @brief 创建 OLED 演示任务和每秒通知该任务的软件定时器。
 * @return 两个静态对象均创建成功返回 1，否则返回 0。
 */
uint8_t AppDisplay_Start(void);

/**
 * @brief 提交第二行的新文字，由唯一显示任务完成硬件写入。
 * @param text 最多21个可打印ASCII字符的零结尾文本。
 * @return 静态消息投递成功返回1，参数无效或模块未启动返回0。
 */
uint8_t AppDisplay_SetKeyText(const char *text);

#endif /* APP_DISPLAY_H */
