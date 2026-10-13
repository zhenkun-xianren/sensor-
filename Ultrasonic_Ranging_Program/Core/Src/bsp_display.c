#include "bsp_display.h"
#include "stm32g071xx.h"

static const uint8_t s_segment_pins[BSP_DISPLAY_SEGMENT_COUNT] = { // A至G对应GPIOB引脚编号。
    BSP_DISPLAY_SEGMENT_A_PIN, BSP_DISPLAY_SEGMENT_B_PIN, BSP_DISPLAY_SEGMENT_C_PIN,
    BSP_DISPLAY_SEGMENT_D_PIN, BSP_DISPLAY_SEGMENT_E_PIN, BSP_DISPLAY_SEGMENT_F_PIN,
    BSP_DISPLAY_SEGMENT_G_PIN
};
static const uint8_t s_digit_pins[BSP_DISPLAY_DIGIT_COUNT] = { // 左到右四个位选的GPIOD引脚编号。
    BSP_DISPLAY_DIG1_PIN, BSP_DISPLAY_DIG2_PIN, BSP_DISPLAY_DIG3_PIN, BSP_DISPLAY_DIG4_PIN
};

/** @brief 保存中断状态后关闭中断，防止任务切换造成显示状态和GPIO读改写竞争。
 * @return 原PRIMASK状态，嵌套调用仍须恢复原值。 */
uint32_t BSP_Display_EnterCritical(void)
{
    uint32_t saved_mask = __get_PRIMASK(); // 保存调用前的中断屏蔽状态。
    __disable_irq();
    __DMB();
    return saved_mask;
}

/** @brief 完成共享状态写入后恢复原中断状态，不擅自开启已屏蔽的中断。
 * @param saved_mask 对应进入临界区前的PRIMASK状态。 */
void BSP_Display_ExitCritical(uint32_t saved_mask)
{
    __DMB();
    __set_PRIMASK(saved_mask);
}

/** @brief 生成受控引脚的两位配置字段掩码及通用输出模式，并只修改这些引脚。
 * @param gpio 待配置的GPIO端口。
 * @param pin_mask 本显示模块拥有的引脚掩码。
 * @return 配置回读一致返回0，任一字段不一致返回非0。 */
static int display_configure_port(GPIO_TypeDef *gpio, uint32_t pin_mask)
{
    uint32_t fields = 0U; // 所有受控引脚的两位配置字段掩码。
    uint32_t output = 0U; // 每个受控引脚的通用输出模式值。
    uint32_t pin; // 遍历端口引脚编号以构造配置字段。
    for (pin = 0U; pin < BSP_DISPLAY_PORT_PIN_COUNT; ++pin) {
        if ((pin_mask & (1UL << pin)) != 0U) {
            fields |= BSP_DISPLAY_GPIO_FIELD_MASK << (pin * BSP_DISPLAY_GPIO_FIELD_WIDTH);
            output |= BSP_DISPLAY_GPIO_MODE_OUTPUT << (pin * BSP_DISPLAY_GPIO_FIELD_WIDTH);
        }
    }
    /* 预置电平由调用者完成；先配置推挽、极低速、无上下拉，再切换为输出，避免误点亮。
     * OSPEEDR=00足够普通1kHz扫描，不影响同端口的其他外设配置。 */
    gpio->OTYPER &= ~pin_mask;
    gpio->OSPEEDR &= ~fields;
    gpio->PUPDR &= ~fields;
    gpio->MODER = (gpio->MODER & ~fields) | output;
    return (((gpio->MODER & fields) == output) && ((gpio->OTYPER & pin_mask) == 0U) &&
            ((gpio->OSPEEDR & fields) == 0U) && ((gpio->PUPDR & fields) == 0U)) ? 0 : -1;
}

/** @brief 通过BSRR先释放四个位选，再关闭全部段线和小数点。 */
void BSP_Display_Blank(void)
{
    GPIOD->BSRR = BSP_DISPLAY_DIGIT_MASK << BSP_DISPLAY_BSRR_RESET_SHIFT;
    GPIOB->BSRR = BSP_DISPLAY_SEGMENT_MASK << BSP_DISPLAY_BSRR_RESET_SHIFT;
    GPIOD->BSRR = BSP_DISPLAY_DP_MASK << BSP_DISPLAY_BSRR_RESET_SHIFT;
}

/** @brief 在短临界区初始化显示GPIO，不修改CubeMX配置或其他引脚。
 * @return 时钟及GPIO配置回读一致返回0，否则返回非0。 */
int BSP_Display_Init(void)
{
    uint32_t saved_mask = BSP_Display_EnterCritical(); // 保护共享RCC及GPIO配置寄存器。
    uint32_t clocks; // RCC时钟开启后的回读，确保后续GPIO访问已可用。
    int result = -1; // 保存两端口配置及关闭电平的检查结果。
    /* RM0444 RCC_IOPENR的GPIOBEN/GPIODEN，开启后回读形成访问顺序。 */
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIODEN;
    clocks = RCC->IOPENR;
    if ((clocks & (RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIODEN)) ==
        (RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIODEN)) {
        BSP_Display_Blank();
        if ((display_configure_port(GPIOB, BSP_DISPLAY_SEGMENT_MASK) == 0) &&
            (display_configure_port(GPIOD, BSP_DISPLAY_PORT_D_MASK) == 0) &&
            ((GPIOB->ODR & BSP_DISPLAY_SEGMENT_MASK) == 0U) &&
            ((GPIOD->ODR & BSP_DISPLAY_PORT_D_MASK) == 0U)) {
            result = 0;
        }
        if (result != 0) {
            BSP_Display_Blank();
        }
    }
    BSP_Display_ExitCritical(saved_mask);
    return result;
}

/** @brief 关闭位选后转换段线映射，输出段码再选通一位，以减少切换重影。
 * @param index 从左到右的数码位编号，0至3。
 * @param glyph A至G及DP的高有效字形位图。
 * @return 0表示完成，非0表示编号越界且未改变输出。 */
int BSP_Display_Output(uint8_t index, uint8_t glyph)
{
    uint32_t segment_set = 0U; // 当前字形需要置高的GPIOB段线。
    uint32_t bit; // 当前检查的A至G字形位索引。
    if (index >= BSP_DISPLAY_DIGIT_COUNT) {
        return -1;
    }
    GPIOD->BSRR = BSP_DISPLAY_DIGIT_MASK << BSP_DISPLAY_BSRR_RESET_SHIFT;
    for (bit = 0U; bit < BSP_DISPLAY_SEGMENT_COUNT; ++bit) {
        if ((glyph & (1U << bit)) != 0U) {
            segment_set |= 1UL << s_segment_pins[bit];
        }
    }
    /* BSRR为只写寄存器，不读改写；置位与复位集合互斥，避免同一引脚同时置/复位。 */
    GPIOB->BSRR = ((BSP_DISPLAY_SEGMENT_MASK & ~segment_set) << BSP_DISPLAY_BSRR_RESET_SHIFT) | segment_set;
    GPIOD->BSRR = ((glyph & BSP_DISPLAY_GLYPH_DP) != 0U) ? BSP_DISPLAY_DP_MASK :
                  (BSP_DISPLAY_DP_MASK << BSP_DISPLAY_BSRR_RESET_SHIFT);
    if (glyph != 0U) {
        GPIOD->BSRR = 1UL << s_digit_pins[index];
    }
    return 0;
}
