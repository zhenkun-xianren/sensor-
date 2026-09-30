#include "bsp_gpio.h"
#include "stm32f103xb.h"

#define BSP_GPIO_POWER_LED_PIN        14UL
#define BSP_GPIO_WARNING_LED_PIN      15UL
#define BSP_GPIO_LED_MASK             ((1UL << BSP_GPIO_POWER_LED_PIN) | \
                                       (1UL << BSP_GPIO_WARNING_LED_PIN))
#define BSP_GPIO_BSRR_RESET_SHIFT     16UL
#define BSP_GPIO_CRH_FIRST_PIN        8UL
#define BSP_GPIO_BITS_PER_PIN         4UL
#define BSP_GPIO_MODE_MASK            0xFUL
#define BSP_GPIO_OUTPUT_PP_2MHZ       0x2UL
#define BSP_GPIO_CRH_SHIFT(pin)       (((pin) - BSP_GPIO_CRH_FIRST_PIN) * \
                                       BSP_GPIO_BITS_PER_PIN)

/**
 * @brief 先预置低电平，再将 PB14、PB15 配为推挽输出以点亮两灯。
 */
void BSP_Gpio_InitIndicators(void)
{
    uint32_t crh; // PB14、PB15 的新引脚模式配置，保留其余引脚状态。

    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    (void)RCC->APB2ENR;

    /* 先写输出锁存器再启用输出，避免模式切换时两灯出现错误状态。 */
    GPIOB->BSRR = BSP_GPIO_LED_MASK << BSP_GPIO_BSRR_RESET_SHIFT;
    crh = GPIOB->CRH;
    crh &= ~((BSP_GPIO_MODE_MASK << BSP_GPIO_CRH_SHIFT(BSP_GPIO_POWER_LED_PIN)) |
             (BSP_GPIO_MODE_MASK << BSP_GPIO_CRH_SHIFT(BSP_GPIO_WARNING_LED_PIN)));
    crh |= (BSP_GPIO_OUTPUT_PP_2MHZ << BSP_GPIO_CRH_SHIFT(BSP_GPIO_POWER_LED_PIN)) |
           (BSP_GPIO_OUTPUT_PP_2MHZ << BSP_GPIO_CRH_SHIFT(BSP_GPIO_WARNING_LED_PIN));
    GPIOB->CRH = crh;
}

/**
 * @brief 检查两路 LED 的输出模式和点亮电平。
 * @return 两灯均配置为低电平输出时返回 1，否则返回 0。
 */
uint8_t BSP_Gpio_IndicatorsAreOn(void)
{
    uint32_t crh = GPIOB->CRH; // 用于核对两路 LED 模式的 GPIO 配置快照。

    return ((GPIOB->ODR & BSP_GPIO_LED_MASK) == 0U &&
            ((crh >> BSP_GPIO_CRH_SHIFT(BSP_GPIO_POWER_LED_PIN)) &
             BSP_GPIO_MODE_MASK) == BSP_GPIO_OUTPUT_PP_2MHZ &&
            ((crh >> BSP_GPIO_CRH_SHIFT(BSP_GPIO_WARNING_LED_PIN)) &
             BSP_GPIO_MODE_MASK) == BSP_GPIO_OUTPUT_PP_2MHZ) ? 1U : 0U;
}

/**
 * @brief 在启动或异常失败时熄灭 WARNING 灯，保留 POWER 灯点亮。
 */
void BSP_Gpio_IndicateFault(void)
{
    GPIOB->BSRR = 1UL << BSP_GPIO_WARNING_LED_PIN;
}
