#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include <stdint.h>

/**
 * @brief 先预置低电平，再将 PB14、PB15 配为推挽输出以点亮两灯。
 */
void BSP_Gpio_InitIndicators(void);

/**
 * @brief 检查两路 LED 的输出模式和点亮电平。
 * @return 两灯均配置为低电平输出时返回 1，否则返回 0。
 */
uint8_t BSP_Gpio_IndicatorsAreOn(void);

/**
 * @brief 在启动或异常失败时熄灭 WARNING 灯，保留 POWER 灯点亮。
 */
void BSP_Gpio_IndicateFault(void);

#endif /* BSP_GPIO_H */
