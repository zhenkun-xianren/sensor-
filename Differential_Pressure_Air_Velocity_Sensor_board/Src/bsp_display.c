#include "bsp_display.h"
#include "bsp_display_pins.h"
#include <stdbool.h>
static bool s_ready = false; // 保存显示引脚最近初始化结果。
/** @brief 保存PRIMASK并屏蔽中断，保护帧提交和共享GPIO字段。
 * @return 调用前中断屏蔽状态。 */
uint32_t BSP_Display_EnterCritical(void)
{
    uint32_t saved = __get_PRIMASK(); // 保存原中断状态。
    __disable_irq(); __DMB(); return saved;
}
/** @brief 恢复原中断屏蔽状态。
 * @param saved_mask 配对进入接口返回的PRIMASK。 */
void BSP_Display_ExitCritical(uint32_t saved_mask) { __DMB(); __set_PRIMASK(saved_mask); }
/** @brief 先关闭所有位选，然后关闭段选与DP；仅操作已开时钟端口。 */
void BSP_Display_Blank(void)
{
    if ((RCC->APB2ENR & RCC_APB2ENR_IOPAEN) != 0U) GPIOA->BSRR = BSP_DISPLAY_A_DIGITS << BSP_BOARD_BSRR_RESET_SHIFT;
    if ((RCC->APB2ENR & RCC_APB2ENR_IOPBEN) != 0U) {
        GPIOB->BSRR = BSP_DISPLAY_B_DIGITS << BSP_BOARD_BSRR_RESET_SHIFT;
        GPIOB->BSRR = BSP_DISPLAY_B_SEGMENTS << BSP_BOARD_BSRR_RESET_SHIFT;
    }
    if ((RCC->APB2ENR & RCC_APB2ENR_IOPAEN) != 0U) GPIOA->BSRR = BSP_DISPLAY_A_DP << BSP_BOARD_BSRR_RESET_SHIFT;
}
/** @brief 配置本板高有效的段选和四位位选，默认关闭。
 * @return 配置读回正常BSP_BOARD_OK，否则BSP_BOARD_ERROR。 */
int BSP_Display_Init(void)
{
    uint32_t saved = BSP_Display_EnterCritical(); // 保护共享GPIO配置。
    int result = BSP_BOARD_ERROR; // 保存初始化状态。
    s_ready = false;
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN;
    (void)RCC->APB2ENR;
    BSP_Display_Blank();
    if (((GPIOA->LCKR & GPIO_LCKR_LCKK) == 0U || (GPIOA->LCKR & (BSP_DISPLAY_A_DIGITS | BSP_DISPLAY_A_DP)) == 0U) &&
        ((GPIOB->LCKR & GPIO_LCKR_LCKK) == 0U || (GPIOB->LCKR & (BSP_DISPLAY_B_DIGITS | BSP_DISPLAY_B_SEGMENTS)) == 0U)) {
        GPIOA->CRH = (GPIOA->CRH & ~BSP_DISPLAY_A_FIELDS) | BSP_DISPLAY_A_MODES;
        GPIOB->CRL = (GPIOB->CRL & ~BSP_DISPLAY_B_LOW_FIELDS) | BSP_DISPLAY_B_LOW_MODES;
        GPIOB->CRH = (GPIOB->CRH & ~BSP_DISPLAY_B_HIGH_FIELDS) | BSP_DISPLAY_B_HIGH_MODES;
        s_ready = ((GPIOA->CRH & BSP_DISPLAY_A_FIELDS) == BSP_DISPLAY_A_MODES &&
            (GPIOB->CRL & BSP_DISPLAY_B_LOW_FIELDS) == BSP_DISPLAY_B_LOW_MODES &&
            (GPIOB->CRH & BSP_DISPLAY_B_HIGH_FIELDS) == BSP_DISPLAY_B_HIGH_MODES);
        if (s_ready) result = BSP_BOARD_OK;
    }
    BSP_Display_ExitCritical(saved);
    return result;
}
/** @brief 先消隐，更新段码，最后仅选通一位；非法参数保持输出。
 * @param index 左到右数码位编号，范围0至3。
 * @param glyph 位0至6为A至G，位7为DP，高有效。
 * @return 成功BSP_BOARD_OK，否则BSP_BOARD_ERROR。 */
int BSP_Display_Output(uint8_t index, uint8_t glyph)
{
    static const uint8_t segment_pins[BSP_DISPLAY_SEG_COUNT] = { BSP_DISPLAY_SEG_A_PIN, BSP_DISPLAY_SEG_B_PIN,
        BSP_DISPLAY_SEG_C_PIN, BSP_DISPLAY_SEG_D_PIN, BSP_DISPLAY_SEG_E_PIN, BSP_DISPLAY_SEG_F_PIN, BSP_DISPLAY_SEG_G_PIN }; // 字形到GPIOB段选的映射。
    static const uint32_t digit_a[BSP_DISPLAY_DIGIT_COUNT] = { GPIO_ODR_ODR12, GPIO_ODR_ODR11, GPIO_ODR_ODR10, 0UL }; // GPIOA上的前三位位选。
    uint32_t segments = 0U; // 本帧GPIOB段选组合。
    uint32_t saved; // 输出更新前的中断屏蔽状态。
    uint8_t bit; // 遍历A至G的字形位。
    if (index >= BSP_DISPLAY_DIGIT_COUNT) return BSP_BOARD_ERROR;
    saved = BSP_Display_EnterCritical();
    if (!s_ready || (GPIOA->CRH & BSP_DISPLAY_A_FIELDS) != BSP_DISPLAY_A_MODES ||
        (GPIOB->CRL & BSP_DISPLAY_B_LOW_FIELDS) != BSP_DISPLAY_B_LOW_MODES ||
        (GPIOB->CRH & BSP_DISPLAY_B_HIGH_FIELDS) != BSP_DISPLAY_B_HIGH_MODES) {
        BSP_Display_Blank(); BSP_Display_ExitCritical(saved); return BSP_BOARD_ERROR;
    }
    BSP_Display_Blank();
    for (bit = 0U; bit < BSP_DISPLAY_SEG_COUNT; ++bit)
        if ((glyph & (BSP_BOARD_HIGH << bit)) != 0U) segments |= BSP_BOARD_HIGH << segment_pins[bit];
    GPIOB->BSRR = segments | ((BSP_DISPLAY_B_SEGMENTS & ~segments) << BSP_BOARD_BSRR_RESET_SHIFT);
    GPIOA->BSRR = (glyph & BSP_DISPLAY_GLYPH_DP_MASK) != 0U ? BSP_DISPLAY_A_DP : (BSP_DISPLAY_A_DP << BSP_BOARD_BSRR_RESET_SHIFT);
    if (glyph != 0U) {
        if (digit_a[index] != 0U) GPIOA->BSRR = digit_a[index];
        else GPIOB->BSRR = BSP_DISPLAY_B_DIGITS;
    }
    BSP_Display_ExitCritical(saved); return BSP_BOARD_OK;
}
