#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>
#include "dev_display.h"

/** @brief 显示模块启动结果。 */
typedef enum {
    APP_DISPLAY_OK = 0, // 设备、默认帧及扫描定时器均已准备完成。
    APP_DISPLAY_FAILED = -1 // 接口、初始化或扫描定时器启动失败。
} AppDisplayResult;

/**
 * @brief 初始化显示设备、提交默认帧并启动静态扫描定时器。
 * @return 成功返回APP_DISPLAY_OK，失败或重复启动返回APP_DISPLAY_FAILED。
 */
AppDisplayResult AppDisplay_Start(void);

/**
 * @brief 查询显示模块是否启动成功且尚未锁存访问故障。
 * @return 可以提交显示内容时返回true，否则返回false。
 */
bool AppDisplay_IsReady(void);

/**
 * @brief 按一位小数和整数前导零补齐模式提交显示数值。
 * @param scaled_value 放大十倍的整数，0至9999，对应000.0至999.9。
 * @return 成功返回DEV_DISPLAY_OK；未启动返回DEV_DISPLAY_NOT_INITIALIZED，
 *         已锁存故障返回DEV_DISPLAY_HARDWARE_ERROR，其他失败沿用设备状态并锁存故障。
 */
DevDisplayStatus AppDisplay_ShowValue(uint16_t scaled_value);

#endif
