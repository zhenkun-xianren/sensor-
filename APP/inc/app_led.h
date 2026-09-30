#ifndef APP_LED_H
#define APP_LED_H

#include <stdint.h>

/**
 * @brief 初始化两路指示灯并启动交替闪烁。
 * @return 设备与周期定时器启动成功返回 1；失败返回 0。
 */
uint8_t AppLed_Init(void);

#endif /* APP_LED_H */
