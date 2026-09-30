#ifndef APP_IR_COUNTER_H
#define APP_IR_COUNTER_H

#include <stdint.h>
#include "dev_ir_remote.h"

uint8_t AppIrCounter_Init(void);

/**
 * @brief 从有效红外数字事件中提取待显示数字。
 * @param event 已解码的红外遥控事件。
 * @param digit 用于接收数字键值的输出指针，范围为 0 至 9。
 * @return event 和 digit 有效且事件为非重复数字键时返回 1，否则返回 0。
 */
uint8_t AppIrCounter_GetDisplayDigit(const IrKeyEvent *event, uint8_t *digit);

#endif
