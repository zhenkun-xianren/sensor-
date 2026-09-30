#ifndef BSP_DISPLAY_MAP_H
#define BSP_DISPLAY_MAP_H

#include <stdint.h>
#include "bsp_display.h"
#include "bsp_gpio.h"

#define BSP_DISPLAY_SEGMENT_COUNT 7U
#define BSP_DISPLAY_DIGIT_INDEX_MASK (BSP_DISPLAY_DIGIT_COUNT - 1U)
#define BSP_DISPLAY_DIGIT_PIN_MASK ((1UL << BSP_GPIO_PA_DISPLAY_DIGIT_1_PIN) | \
                                    (1UL << BSP_GPIO_PA_DISPLAY_DIGIT_2_PIN) | \
                                    (1UL << BSP_GPIO_PA_DISPLAY_DIGIT_3_PIN) | \
                                    (1UL << BSP_GPIO_PA_DISPLAY_DIGIT_4_PIN))
#define BSP_DISPLAY_DP_PIN_MASK (1UL << BSP_GPIO_PA_DISPLAY_DP_PIN)
#define BSP_DISPLAY_SEGMENT_PIN_MASK BSP_GPIO_PB_DISPLAY_SEGMENT_MASK

/**
 * @brief 将显示字形中的 A 至 G 段映射为当前板级 GPIO 掩码。
 * @param glyph 七段字形位图，位 0 至 6 对应 A 至 G。
 * @return 当前板级段选引脚对应的 GPIOB 输出掩码。
 */
uint16_t BSP_Display_SegmentPins(uint8_t glyph);

/**
 * @brief 获取选通指定数码位的当前板级 GPIO 掩码。
 * @param index 数码位索引，0 至 3 依次对应 DIG1 至 DIG4。
 * @return 对应的 GPIOA 输出掩码；索引越界时返回 0。
 */
uint16_t BSP_Display_DigitPin(uint8_t index);

#endif
