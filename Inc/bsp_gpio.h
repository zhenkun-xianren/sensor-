#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include <stdint.h>

#define BSP_GPIO_BSRR_RESET_SHIFT            16U
#define BSP_GPIO_PA_OPTOCOUPLER_PIN            1U
#define BSP_GPIO_PA_RELAY_PIN                  5U
#define BSP_GPIO_PA_BUZZER_PIN                 6U
#define BSP_GPIO_PB_POWER_LED_PIN             14U
#define BSP_GPIO_PB_WARNING_LED_PIN           15U
#define BSP_GPIO_PA_SAFE_HIGH_MASK            (1UL << BSP_GPIO_PA_OPTOCOUPLER_PIN)
#define BSP_GPIO_PA_DISPLAY_DIGIT_1_PIN      11U
#define BSP_GPIO_PA_DISPLAY_DIGIT_2_PIN      10U
#define BSP_GPIO_PA_DISPLAY_DIGIT_3_PIN       9U
#define BSP_GPIO_PA_DISPLAY_DIGIT_4_PIN      12U
#define BSP_GPIO_PA_DISPLAY_DP_PIN           15U
#define BSP_GPIO_PB_DISPLAY_SEGMENT_A_PIN     9U
#define BSP_GPIO_PB_DISPLAY_SEGMENT_B_PIN     8U
#define BSP_GPIO_PB_DISPLAY_SEGMENT_C_PIN     7U
#define BSP_GPIO_PB_DISPLAY_SEGMENT_D_PIN     6U
#define BSP_GPIO_PB_DISPLAY_SEGMENT_E_PIN     5U
#define BSP_GPIO_PB_DISPLAY_SEGMENT_F_PIN     4U
#define BSP_GPIO_PB_DISPLAY_SEGMENT_G_PIN     3U
#define BSP_GPIO_PB_DISPLAY_SEGMENT_MASK     (0x7FUL << BSP_GPIO_PB_DISPLAY_SEGMENT_G_PIN)

/**
 * @brief 先预置低电平，再将 PB14、PB15 配为推挽输出以点亮两灯。
 */
void BSP_Gpio_InitIndicators(void);

/**
 * @brief 将 PA1、PA4、PA5、PA6 预置为现场输出、RS485 接收、继电器和蜂鸣器的安全状态。
 */
void BSP_Gpio_InitSafeOutputs(void);

/**
 * @brief 释放显示用 JTAG 引脚并配置四位位选、段选及小数点输出。
 * @return 显示引脚、安全输出和两灯状态均正确返回 1，否则返回 0。
 */
uint8_t BSP_Gpio_InitDisplayPins(void);

/**
 * @brief 检查两路 LED 的输出模式和点亮电平。
 * @return 两灯均配置为低电平输出时返回 1，否则返回 0。
 */
uint8_t BSP_Gpio_IndicatorsAreOn(void);

/**
 * @brief 检查 PA1、PA4、PA5、PA6 的模式与安全电平。
 * @return 四路输出均处于预定关闭状态返回 1，否则返回 0。
 */
uint8_t BSP_Gpio_SafeOutputsAreOff(void);

/**
 * @brief 在启动或异常失败时熄灭 WARNING 灯，保留 POWER 灯点亮。
 */
void BSP_Gpio_IndicateFault(void);

#endif /* BSP_GPIO_H */
