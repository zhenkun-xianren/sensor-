#include "bsp_display_map.h"
#include "bsp_gpio.h"

/**
 * @brief 将七段数码管字形位转换为板级 GPIO 引脚掩码。
 * @param glyph 选择 A 至 G 段的位字段。
 * @return 对应数码管段选信号的 GPIOB 输出掩码。
 */
uint16_t BSP_Display_SegmentPins(uint8_t glyph)
{
    static const uint8_t segment_pins[BSP_DISPLAY_SEGMENT_COUNT] = { // 七段字形各段对应的 GPIO 引脚编号表。
        BSP_GPIO_PB_DISPLAY_SEGMENT_A_PIN,
        BSP_GPIO_PB_DISPLAY_SEGMENT_B_PIN,
        BSP_GPIO_PB_DISPLAY_SEGMENT_C_PIN,
        BSP_GPIO_PB_DISPLAY_SEGMENT_D_PIN,
        BSP_GPIO_PB_DISPLAY_SEGMENT_E_PIN,
        BSP_GPIO_PB_DISPLAY_SEGMENT_F_PIN,
        BSP_GPIO_PB_DISPLAY_SEGMENT_G_PIN
    };
    uint16_t pins = 0U; // 当前数码管字形对应的段选引脚掩码。
    uint8_t bit; // 遍历字形位图各段的索引。
    for (bit = 0U; bit < BSP_DISPLAY_SEGMENT_COUNT; ++bit) {
        if ((glyph & (1U << bit)) != 0U) pins |= (uint16_t)(1U << segment_pins[bit]);
    }
    return pins;
}

/**
 * @brief 返回选通一个扫描数码位所需的 GPIO 掩码。
 * @param index 按显示顺序排列的从零开始的数码位索引。
 * @return 对应数码位的 GPIOA 输出掩码；索引越界时返回 0。
 */
uint16_t BSP_Display_DigitPin(uint8_t index)
{
    static const uint16_t pins[BSP_DISPLAY_DIGIT_COUNT] = { // 四个数码位选通信号对应的 GPIO 掩码表。
        1U << BSP_GPIO_PA_DISPLAY_DIGIT_1_PIN,
        1U << BSP_GPIO_PA_DISPLAY_DIGIT_2_PIN,
        1U << BSP_GPIO_PA_DISPLAY_DIGIT_3_PIN,
        1U << BSP_GPIO_PA_DISPLAY_DIGIT_4_PIN
    };
    return index < BSP_DISPLAY_DIGIT_COUNT ? pins[index] : 0U;
}
