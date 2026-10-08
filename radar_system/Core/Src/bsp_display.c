#include "bsp_display_pins.h"

/**
 * @brief 保存中断状态并进入短临界区，保护共享帧及GPIO输出顺序。
 * @return 调用前的PRIMASK，须传给对应退出接口恢复。
 */
uint32_t BSP_Display_EnterCritical(void)
{
    uint32_t saved_mask = __get_PRIMASK(); // 保存调用前的中断屏蔽状态。
    __disable_irq();
    __DMB();
    return saved_mask;
}

/**
 * @brief 完成共享数据访问后恢复原中断状态。
 * @param saved_mask 配套进入接口返回的PRIMASK，不强制打开原已关闭的中断。
 */
void BSP_Display_ExitCritical(uint32_t saved_mask)
{
    __DMB();
    __set_PRIMASK(saved_mask);
}

/**
 * @brief 先关闭全部位选，再关闭段选和小数点；须在所属GPIO时钟开启后调用。
 */
void BSP_Display_Blank(void)
{
    GPIOA->BSRR = BSP_DISPLAY_DIGIT_MASK << BSP_DISPLAY_BSRR_RESET_SHIFT;
    GPIOA->BSRR = BSP_DISPLAY_GPIOA_SEGMENT_MASK << BSP_DISPLAY_BSRR_RESET_SHIFT;
    GPIOB->BSRR = BSP_DISPLAY_GPIOB_SEGMENT_MASK << BSP_DISPLAY_BSRR_RESET_SHIFT;
}

/**
 * @brief 初始化三位FJ2352AH所属GPIO，预置关闭电平并核对寄存器配置。
 * @return 初始化及配置读回成功返回BSP_DISPLAY_OK，否则返回BSP_DISPLAY_ERROR。
 * @details 仅修改正式引脚表中的11根显示线，不改时钟树、复用或SWD引脚。
 */
int BSP_Display_Init(void)
{
    uint32_t saved_mask = BSP_Display_EnterCritical(); // 保护共享RCC和GPIO配置寄存器。
    int result = BSP_DISPLAY_OK; // 保存寄存器初始化结果，不能代表实物已关闭。
    RCC->APB2ENR |= BSP_DISPLAY_CLOCK_MASK;
    /* 读回时钟使能后才访问GPIO；所有所属引脚先写低，再切换为推挽输出。 */
    if ((RCC->APB2ENR & BSP_DISPLAY_CLOCK_MASK) != BSP_DISPLAY_CLOCK_MASK) {
        BSP_Display_ExitCritical(saved_mask);
        return BSP_DISPLAY_ERROR;
    }
    BSP_Display_Blank();
    GPIOA->CRL = (GPIOA->CRL & ~BSP_DISPLAY_GPIOA_CRL_MASK) | BSP_DISPLAY_GPIOA_CRL_OUTPUT;
    GPIOA->CRH = (GPIOA->CRH & ~BSP_DISPLAY_GPIOA_CRH_MASK) | BSP_DISPLAY_GPIOA_CRH_OUTPUT;
    GPIOB->CRL = (GPIOB->CRL & ~BSP_DISPLAY_GPIOB_CRL_MASK) | BSP_DISPLAY_GPIOB_CRL_OUTPUT;
    GPIOB->CRH = (GPIOB->CRH & ~BSP_DISPLAY_GPIOB_CRH_MASK) | BSP_DISPLAY_GPIOB_CRH_OUTPUT;
    if (((GPIOA->CRL & BSP_DISPLAY_GPIOA_CRL_MASK) != BSP_DISPLAY_GPIOA_CRL_OUTPUT) ||
        ((GPIOA->CRH & BSP_DISPLAY_GPIOA_CRH_MASK) != BSP_DISPLAY_GPIOA_CRH_OUTPUT) ||
        ((GPIOB->CRL & BSP_DISPLAY_GPIOB_CRL_MASK) != BSP_DISPLAY_GPIOB_CRL_OUTPUT) ||
        ((GPIOB->CRH & BSP_DISPLAY_GPIOB_CRH_MASK) != BSP_DISPLAY_GPIOB_CRH_OUTPUT) ||
        ((GPIOA->ODR & (BSP_DISPLAY_GPIOA_SEGMENT_MASK | BSP_DISPLAY_DIGIT_MASK)) != BSP_DISPLAY_GLYPH_BLANK) ||
        ((GPIOB->ODR & BSP_DISPLAY_GPIOB_SEGMENT_MASK) != BSP_DISPLAY_GLYPH_BLANK)) {
        BSP_Display_Blank();
        result = BSP_DISPLAY_ERROR;
    }
    BSP_Display_ExitCritical(saved_mask);
    return result;
}

/**
 * @brief 先关闭位选，更新跨端口段码，最后选通左到右的一个实际数码位。
 * @param index 左、中、右三位分别为0、1、2，越界时保持原输出。
 * @param glyph A～G和DP的高有效字形，空白字形不选通。
 * @return 输出写入完成返回BSP_DISPLAY_OK，非法位置返回BSP_DISPLAY_ERROR；不检测实物反馈。
 * @details Port在临界区内调用本接口；BSRR只写，不读改写，不触碰其他模块引脚。
 */
int BSP_Display_Output(uint8_t index, uint8_t glyph)
{
    static const uint32_t digit_pins[BSP_DISPLAY_DIGIT_COUNT] = { // 左到右三路NPN基极控制。
        BSP_DISPLAY_DIGIT_1_PIN, BSP_DISPLAY_DIGIT_2_PIN, BSP_DISPLAY_DIGIT_3_PIN
    };
    uint32_t segment_a = BSP_DISPLAY_GLYPH_BLANK; // 本帧GPIOA所属段线的置位集合。
    uint32_t segment_b = BSP_DISPLAY_GLYPH_BLANK; // 本帧GPIOB所属段线的置位集合。
    if (index >= BSP_DISPLAY_DIGIT_COUNT) {
        return BSP_DISPLAY_ERROR;
    }
    GPIOA->BSRR = BSP_DISPLAY_DIGIT_MASK << BSP_DISPLAY_BSRR_RESET_SHIFT;
    if ((glyph & BSP_DISPLAY_GLYPH_A_MASK) != BSP_DISPLAY_GLYPH_BLANK) { segment_b |= BSP_DISPLAY_SEGMENT_A_PIN; }
    if ((glyph & BSP_DISPLAY_GLYPH_B_MASK) != BSP_DISPLAY_GLYPH_BLANK) { segment_a |= BSP_DISPLAY_SEGMENT_B_PIN; }
    if ((glyph & BSP_DISPLAY_GLYPH_C_MASK) != BSP_DISPLAY_GLYPH_BLANK) { segment_b |= BSP_DISPLAY_SEGMENT_C_PIN; }
    if ((glyph & BSP_DISPLAY_GLYPH_D_MASK) != BSP_DISPLAY_GLYPH_BLANK) { segment_b |= BSP_DISPLAY_SEGMENT_D_PIN; }
    if ((glyph & BSP_DISPLAY_GLYPH_E_MASK) != BSP_DISPLAY_GLYPH_BLANK) { segment_b |= BSP_DISPLAY_SEGMENT_E_PIN; }
    if ((glyph & BSP_DISPLAY_GLYPH_F_MASK) != BSP_DISPLAY_GLYPH_BLANK) { segment_b |= BSP_DISPLAY_SEGMENT_F_PIN; }
    if ((glyph & BSP_DISPLAY_GLYPH_G_MASK) != BSP_DISPLAY_GLYPH_BLANK) { segment_a |= BSP_DISPLAY_SEGMENT_G_PIN; }
    if ((glyph & BSP_DISPLAY_GLYPH_DP_MASK) != BSP_DISPLAY_GLYPH_BLANK) { segment_b |= BSP_DISPLAY_SEGMENT_DP_PIN; }
    /* 置位和复位集合互斥；在所有位选关闭期间完成两端口段线切换。 */
    GPIOA->BSRR = ((BSP_DISPLAY_GPIOA_SEGMENT_MASK & ~segment_a) << BSP_DISPLAY_BSRR_RESET_SHIFT) | segment_a;
    GPIOB->BSRR = ((BSP_DISPLAY_GPIOB_SEGMENT_MASK & ~segment_b) << BSP_DISPLAY_BSRR_RESET_SHIFT) | segment_b;
    if (glyph != BSP_DISPLAY_GLYPH_BLANK) {
        GPIOA->BSRR = digit_pins[index];
    }
    return BSP_DISPLAY_OK;
}
