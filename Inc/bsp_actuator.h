#ifndef BSP_ACTUATOR_H
#define BSP_ACTUATOR_H

#include <stdint.h>

/**
 * @brief 将继电器、隔离输出和蜂鸣器置为安全状态并核对两灯。
 * @return 安全初值和两灯正确返回 1，否则返回 0。
 */
uint8_t BSP_Actuator_Init(void);

/**
 * @brief 设置继电器驱动状态。
 * @param enabled 非零时吸合继电器，零时释放继电器。
 * @return 输出锁存状态与要求一致返回 1，否则返回 0。
 */
uint8_t BSP_Actuator_SetRelay(uint8_t enabled);

/**
 * @brief 设置反相的 PC817 隔离现场输出。
 * @param enabled 非零时使现场输出有效，零时关闭。
 * @return 输出锁存状态与要求一致返回 1，否则返回 0。
 */
uint8_t BSP_Actuator_SetOptocoupler(uint8_t enabled);

/**
 * @brief 设置 PA6 蜂鸣器状态。
 * @param enabled 非零时使蜂鸣器鸣叫，零时关闭。
 * @return 输出锁存状态与要求一致返回 1，否则返回 0。
 */
uint8_t BSP_Actuator_SetBuzzer(uint8_t enabled);

/**
 * @brief 按低电平点亮方式设置两路指示灯。
 * @param warning_enabled 非零时点亮 WARNING，零时熄灭。
 * @param power_enabled 非零时点亮 POWER，零时熄灭。
 * @return 两路输出锁存状态与要求一致返回 1，否则返回 0。
 */
uint8_t BSP_Actuator_SetIndicatorLeds(uint8_t warning_enabled, uint8_t power_enabled);

#endif
