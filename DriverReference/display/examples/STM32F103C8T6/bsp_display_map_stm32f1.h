#ifndef BSP_DISPLAY_MAP_STM32F1_H
#define BSP_DISPLAY_MAP_STM32F1_H

#include <stdint.h>
#include "bsp_display.h"
#include "bsp_display_stm32f1_pins.h"

#define BSP_DISPLAY_STM32F1_SEGMENT_COUNT 7U

/**
 * @brief 将 A 至 G 段字形位映射到 STM32F103 GPIOB 引脚。
 * @param glyph 七段字形位图，位 0 至 6 对应 A 至 G。
 * @return 对应的 GPIOB 输出掩码。
 */
uint16_t BSP_Display_STM32F1_SegmentPins(uint8_t glyph);

/**
 * @brief 获取选通指定数码位的 STM32F103 GPIOA 掩码。
 * @param index 数码位索引，0 至 3 依次对应 DIG1 至 DIG4。
 * @return 对应 GPIOA 输出掩码；索引越界时返回 0。
 */
uint16_t BSP_Display_STM32F1_DigitPin(uint8_t index);

#endif
