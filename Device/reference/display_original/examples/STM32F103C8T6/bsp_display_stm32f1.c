/* 与统一Device/Port配套的STM32F103参考；本次未编译，硬件未验证。 */
#include "bsp_display.h"
#include "bsp_display_map_stm32f1.h"
#include "stm32f103xb.h"

/**
 * @brief 保存中断状态并关闭中断，保护Port共享状态与GPIO输出顺序。
 * @return 调用前PRIMASK值，退出临界区时必须恢复同一值。
 */
uint32_t BSP_Display_EnterCritical(void)
{
    uint32_t saved_mask = __get_PRIMASK(); // 保存调用前的中断屏蔽状态。
    __disable_irq();
    __DMB();
    return saved_mask;
}

/**
 * @brief 完成共享状态写入后恢复进入前的中断状态。
 * @param saved_mask 对应进入临界区接口返回的PRIMASK值。
 */
void BSP_Display_ExitCritical(uint32_t saved_mask)
{
    __DMB();
    __set_PRIMASK(saved_mask);
}

/**
 * @brief 先释放全部位选，再关闭段线和小数点；调用方须已配置GPIO。
 */
void BSP_Display_Blank(void)
{
    GPIOA->BSRR = BSP_DISPLAY_STM32F1_DIGIT_PIN_MASK << BSP_DISPLAY_STM32F1_BSRR_RESET_SHIFT;
    GPIOB->BSRR = BSP_DISPLAY_STM32F1_SEGMENT_PIN_MASK << BSP_DISPLAY_STM32F1_BSRR_RESET_SHIFT;
    GPIOA->BSRR = BSP_DISPLAY_STM32F1_DP_PIN_MASK << BSP_DISPLAY_STM32F1_BSRR_RESET_SHIFT;
}

/**
 * @brief 关闭显示输出，复用参考包已有GPIO映射；帧与扫描位置由Port初始化。
 * @return 0表示关闭写入已发出；本参考不检测时钟、GPIO配置或实物反馈。
 * @details 调用方必须先开启GPIOA/GPIOB时钟，配置所属引脚输出及PA15普通GPIO功能。
 */
int BSP_Display_Init(void)
{
    BSP_Display_Blank();
    return 0;
}

/**
 * @brief 沿用参考引脚映射，先关闭位选，再换段码，最后选通指定数码位。
 * @param index 从左到右的数码位编号，范围为0至3。
 * @param glyph A至G及小数点的高有效字形位图。
 * @return 0表示输出写入已发出，非法编号返回非0且保持原输出；不检测实物状态。
 */
int BSP_Display_Output(uint8_t index, uint8_t glyph)
{
    uint32_t segment_set; // 保存当前字形对应的GPIOB段选置位掩码。
    if (index >= BSP_DISPLAY_DIGIT_COUNT) {
        return -1;
    }
    GPIOA->BSRR = BSP_DISPLAY_STM32F1_DIGIT_PIN_MASK << BSP_DISPLAY_STM32F1_BSRR_RESET_SHIFT;
    segment_set = BSP_Display_STM32F1_SegmentPins(glyph);
    /* BSRR只写；置位和复位集合互斥，沿用原映射并避免切换时旧段残留。 */
    GPIOB->BSRR = ((BSP_DISPLAY_STM32F1_SEGMENT_PIN_MASK & ~segment_set) <<
                   BSP_DISPLAY_STM32F1_BSRR_RESET_SHIFT) | segment_set;
    GPIOA->BSRR = BSP_DISPLAY_STM32F1_DP_PIN_MASK <<
                  ((glyph & BSP_DISPLAY_GLYPH_DP_MASK) != 0U ? 0U : BSP_DISPLAY_STM32F1_BSRR_RESET_SHIFT);
    if (glyph != 0U) {
        GPIOA->BSRR = BSP_Display_STM32F1_DigitPin(index);
    }
    return 0;
}
