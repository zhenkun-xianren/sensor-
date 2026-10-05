#ifndef BSP_TIME_H
#define BSP_TIME_H

#include <stdint.h>

#define BSP_TIME_TICK_HZ 1000UL

/**
 * @brief 校验 SysTick 装载值并启动 OLED/RS485 共享的 DWT，保留 FreeRTOS 的 SysTick 所有权。
 * @return 装载值和周期计数器有效返回 1，否则返回 0。
 */
uint8_t BSP_Time_Init(void);

/**
 * @brief 获取开机以来的毫秒计数，允许自然回绕。
 * @return 当前毫秒计数。
 */
uint32_t BSP_Time_GetMs(void);

/**
 * @brief 在 SysTick 中断中推进板级毫秒计数。
 */
void BSP_Time_OnSysTick(void);

#endif /* BSP_TIME_H */
