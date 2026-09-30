#include "bsp_display.h"
#include "bsp_display_map.h"
#include "bsp_gpio.h"
#include "stm32f103xb.h"

static volatile uint8_t s_frame[BSP_DISPLAY_DIGIT_COUNT]; // 待扫描显示的各位段码缓冲区。
static uint8_t s_current; // 下一次扫描要点亮的数码管位索引。
static volatile uint8_t s_scan_enabled; // 数码管扫描是否已启用。

/**
 * @brief 初始化数码管 GPIO 输出和扫描状态。
 * @return 引脚与安全状态检查通过返回 1，否则返回 0。
 */
uint8_t BSP_Display_Init(void)
{
    uint8_t index; // 遍历四个数码管字形缓冲单元的索引。

    if (BSP_Gpio_InitDisplayPins() == 0U) {
        return 0U;
    }
    GPIOA->BSRR = BSP_DISPLAY_DIGIT_PIN_MASK << BSP_GPIO_BSRR_RESET_SHIFT;
    GPIOA->BSRR = BSP_DISPLAY_DP_PIN_MASK << BSP_GPIO_BSRR_RESET_SHIFT;
    GPIOB->BSRR = BSP_DISPLAY_SEGMENT_PIN_MASK << BSP_GPIO_BSRR_RESET_SHIFT;
    for (index = 0U; index < BSP_DISPLAY_DIGIT_COUNT; ++index) {
        s_frame[index] = 0U;
    }
    s_current = 0U;
    s_scan_enabled = 1U;
    return 1U;
}

/**
 * @brief 原子更新供扫描软件定时器使用的四个字形数据。
 * @param frame 按 DIG1 至 DIG4 顺序排列的字形字节。
 */
void BSP_Display_SetFrame(const uint8_t frame[BSP_DISPLAY_DIGIT_COUNT])
{
    uint8_t index; // 遍历显示缓冲区各位的索引。
    uint32_t primask = __get_PRIMASK(); // 进入临界区前保存的中断屏蔽状态。
    __disable_irq();
    for (index = 0U; index < BSP_DISPLAY_DIGIT_COUNT; ++index) s_frame[index] = frame[index];
    s_scan_enabled = 1U;
    if (primask == 0U) __enable_irq();
}

/**
 * @brief 原子更新指定数码位的字形并启用动态扫描。
 * @param index 要更新的数码位下标，按 DIG1 至 DIG4 对应 0 至 3。
 * @param glyph 要显示的七段字形及小数点位字段。
 */
void BSP_Display_SetDigit(uint8_t index, uint8_t glyph)
{
    uint32_t primask; // 用于保存并恢复更新显示缓冲区时的中断屏蔽状态。

    if (index >= BSP_DISPLAY_DIGIT_COUNT) return;

    primask = __get_PRIMASK();
    __disable_irq();
    s_frame[index] = glyph;
    s_scan_enabled = 1U;
    if (primask == 0U) __enable_irq();
}

/**
 * @brief 关闭全部数码管位选，避免更新段码时出现重影。
 */
static inline void BSP_Display_BlankDigits(void)
{
    GPIOA->BSRR = BSP_DISPLAY_DIGIT_PIN_MASK << BSP_GPIO_BSRR_RESET_SHIFT;
}

/**
 * @brief 先清除全部段选输出，再设置当前字形对应的段选。
 * @param segment_set 当前字形需要置位的 GPIOB 段选掩码。
 */
static inline void BSP_Display_WriteSegments(uint16_t segment_set)
{
    GPIOB->BSRR = (BSP_DISPLAY_SEGMENT_PIN_MASK << BSP_GPIO_BSRR_RESET_SHIFT) |
                  segment_set;
}

/**
 * @brief 根据字形中的小数点标志设置或清除小数点输出。
 * @param glyph 当前数码位的字形及小数点标志。
 */
static inline void BSP_Display_WriteDecimalPoint(uint8_t glyph)
{
    GPIOA->BSRR = BSP_DISPLAY_DP_PIN_MASK <<
                  ((glyph & BSP_DISPLAY_GLYPH_DP_MASK) != 0U ?
                       0U : BSP_GPIO_BSRR_RESET_SHIFT);
}

/**
 * @brief 选通指定索引对应的数码管位。
 * @param index 要选通的数码位索引，0 至 3 依次对应 DIG1 至 DIG4。
 */
static inline void BSP_Display_EnableDigit(uint8_t index)
{
    GPIOA->BSRR = BSP_Display_DigitPin(index);
}

/**
 * @brief 切换到下一位数码管并更新段选输出。
 */
void BSP_Display_ScanStep(void)
{
    uint8_t glyph; // 当前待显示数字对应的段码。
    uint16_t segment_set; // 当前段码转换得到的段选引脚掩码。
    if (s_scan_enabled == 0U) return;
    BSP_Display_BlankDigits();
    glyph = s_frame[s_current];
    segment_set = BSP_Display_SegmentPins(glyph);
    BSP_Display_WriteSegments(segment_set);
    BSP_Display_WriteDecimalPoint(glyph);
    BSP_Display_EnableDigit(s_current);
    s_current = (uint8_t)((s_current + 1U) & BSP_DISPLAY_DIGIT_INDEX_MASK);
}
