#ifndef APP_IR_RELAY_H
#define APP_IR_RELAY_H

#include <stdint.h>

#define APP_IR_RELAY_STATUS_PENDING 0U
#define APP_IR_RELAY_STATUS_READY   1U
#define APP_IR_RELAY_STATUS_ERROR  2U
#define APP_IR_RELAY_STATUS_SAFE   3U
#define APP_IR_RELAY_STATUS_SAFE_FAILURE 4U

extern volatile uint8_t g_app_ir_relay_status; // 红外遥控任务的初始化和运行状态，供 SWD 观察。

/**
 * @brief 初始化红外接收、按键提示、数值显示及继电器控制任务。
 * @return 红外接收、执行器、提示定时器及静态任务均成功创建返回 1，否则返回 0。
 */
uint8_t AppIrRelay_Init(void);

#endif /* APP_IR_RELAY_H */
