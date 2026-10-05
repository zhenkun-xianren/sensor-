#ifndef APP_IR_REMOTE_H
#define APP_IR_REMOTE_H
#include <stdint.h>
#include "app_display.h"
/** @brief 红外功能运行状态。 */
typedef enum {
    APP_IR_PENDING = 0, /**< 尚未启动。 */
    APP_IR_RUNNING, /**< 接收设备已初始化并读取事件。 */
    APP_IR_ERROR /**< 初始化、读取或显示消息提交失败。 */
} AppIrStatus;
extern volatile AppIrStatus g_app_ir_status; // 红外模块的运行状态。
extern volatile uint32_t g_app_ir_events; // 已成功提交显示的有效事件数，含重复帧。
extern volatile uint32_t g_app_ir_raw_code; // 最近有效事件的原始帧。
extern volatile uint16_t g_app_ir_address; // 最近有效遥控器地址。
extern volatile uint8_t g_app_ir_command; // 最近有效命令字节。
extern volatile uint8_t g_app_ir_repeat; // 最近事件是否为重复帧。
/**
 * @brief 将命令映射为已确认布局的21键名称或未知键码文字。
 * @param command 命令字节，范围0至255。
 * @param text 接收文字的缓存，容量至少APP_DISPLAY_TEXT_CAPACITY字节。
 * @return 生成成功返回1，缓存指针为空返回0。
 */
uint8_t AppIr_FormatKey(uint8_t command, char text[APP_DISPLAY_TEXT_CAPACITY]);
/**
 * @brief 创建独立红外任务，设备在任务中初始化。
 * @return 静态任务创建成功返回1，失败返回0。
 */
uint8_t AppIrRemote_Start(void);
#endif
