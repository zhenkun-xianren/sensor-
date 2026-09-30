#ifndef APP_MODBUS_H
#define APP_MODBUS_H

#include <stdint.h>
#include "dev_modbus.h"

/**
 * @brief 启动 Modbus RTU 周期写寄存器模块。
 * @return 设备初始化、静态任务创建和软件定时器启动均成功时返回 1，否则返回 0。
 */
uint8_t AppModbus_Init(void);

/**
 * @brief 读取最近一次周期写寄存器事务的结果。
 * @return 最近一次事务的 Modbus 状态；模块尚未启动时返回 DEV_MODBUS_NOT_INITIALIZED。
 */
DevModbusStatus AppModbus_GetLastStatus(void);

#endif /* APP_MODBUS_H */
