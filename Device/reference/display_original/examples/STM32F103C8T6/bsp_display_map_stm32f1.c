#include "bsp_display_map_stm32f1.h"

/**
 * @brief 将 A 至 G 段字形位映射到 STM32F103 GPIOB 引脚。
 * @param glyph 七段字形位图，位 0 至 6 对应 A 至 G。
 * @return 对应的 GPIOB 输出掩码。
 */
uint16_t BSP_Display_STM32F1_SegmentPins(uint8_t glyph)
{
    static const uint8_t segment_pins[BSP_DISPLAY_STM32F1_SEGMENT_COUNT] = {
        BSP_DISPLAY_STM32F1_SEGMENT_A_PIN,
        BSP_DISPLAY_STM32F1_SEGMENT_B_PIN,
        BSP_DISPLAY_STM32F1_SEGMENT_C_PIN,
        BSP_DISPLAY_STM32F1_SEGMENT_D_PIN,
        BSP_DISPLAY_STM32F1_SEGMENT_E_PIN,
        BSP_DISPLAY_STM32F1_SEGMENT_F_PIN,
        BSP_DISPLAY_STM32F1_SEGMENT_G_PIN
    };
    uint16_t pins = 0U;
    uint8_t bit;

    for (bit = 0U; bit < BSP_DISPLAY_STM32F1_SEGMENT_COUNT; ++bit) {
        if ((glyph & (1U << bit)) != 0U) {
            pins |= (uint16_t)(1U << segment_pins[bit]);
        }
    }
    return pins;
}

/**
 * @brief 获取选通指定数码位的 STM32F103 GPIOA 掩码。
 * @param index 数码位索引，0 至 3 依次对应 DIG1 至 DIG4。
 * @return 对应 GPIOA 输出掩码；索引越界时返回 0。
 */
uint16_t BSP_Display_STM32F1_DigitPin(uint8_t index)
{
    static const uint16_t pins[BSP_DISPLAY_DIGIT_COUNT] = {
        1U << BSP_DISPLAY_STM32F1_DIGIT_1_PIN,
        1U << BSP_DISPLAY_STM32F1_DIGIT_2_PIN,
        1U << BSP_DISPLAY_STM32F1_DIGIT_3_PIN,
        1U << BSP_DISPLAY_STM32F1_DIGIT_4_PIN
    };

    return index < BSP_DISPLAY_DIGIT_COUNT ? pins[index] : 0U;
}
