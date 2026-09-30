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
#define BSP_GPIO_CRL_SHIFT(pin)       ((pin) * BSP_GPIO_BITS_PER_PIN)
#define BSP_GPIO_OPTO_PIN             1UL
#define BSP_GPIO_RS485_DIR_PIN        4UL
#define BSP_GPIO_RELAY_PIN            5UL
#define BSP_GPIO_BUZZER_PIN           6UL
#define BSP_GPIO_SAFE_HIGH_MASK       (1UL << BSP_GPIO_OPTO_PIN)
#define BSP_GPIO_SAFE_LOW_MASK        ((1UL << BSP_GPIO_RS485_DIR_PIN) | \
                                       (1UL << BSP_GPIO_RELAY_PIN) | \
                                       (1UL << BSP_GPIO_BUZZER_PIN))
#define BSP_GPIO_SAFE_OUTPUT_MASK     (BSP_GPIO_SAFE_HIGH_MASK | \
                                       BSP_GPIO_SAFE_LOW_MASK)

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
 * @brief 将 PA1、PA4、PA5、PA6 预置为现场输出、RS485 接收、继电器和蜂鸣器的安全状态。
 */
void BSP_Gpio_InitSafeOutputs(void)
{
    uint32_t crl; // 四路安全输出的新 GPIOA 模式配置，保留其余引脚设置。
    uint32_t pin; // 当前要配置为推挽输出的 PA 引脚编号。

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    (void)RCC->APB2ENR;

    /* PA1 高使 PC817 导通并将现场输出拉低；PA4、PA5、PA6 低保持接收、继电器和蜂鸣器关闭。 */
    GPIOA->BSRR = BSP_GPIO_SAFE_HIGH_MASK |
                  (BSP_GPIO_SAFE_LOW_MASK << BSP_GPIO_BSRR_RESET_SHIFT);
    crl = GPIOA->CRL;
    for (pin = BSP_GPIO_OPTO_PIN; pin <= BSP_GPIO_BUZZER_PIN; ++pin) {
        if ((BSP_GPIO_SAFE_OUTPUT_MASK & (1UL << pin)) != 0U) {
            crl = (crl & ~(BSP_GPIO_MODE_MASK << BSP_GPIO_CRL_SHIFT(pin))) |
                  (BSP_GPIO_OUTPUT_PP_2MHZ << BSP_GPIO_CRL_SHIFT(pin));
        }
    }
    GPIOA->CRL = crl;
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
 * @brief 检查 PA1、PA4、PA5、PA6 的模式与安全电平。
 * @return 四路输出均处于预定关闭状态返回 1，否则返回 0。
 */
uint8_t BSP_Gpio_SafeOutputsAreOff(void)
{
    uint32_t crl = GPIOA->CRL; // 用于核对安全输出模式的 GPIOA 配置快照。
    uint32_t pin; // 当前核对的 PA 安全输出引脚编号。

    if ((GPIOA->ODR & BSP_GPIO_SAFE_HIGH_MASK) != BSP_GPIO_SAFE_HIGH_MASK ||
        (GPIOA->ODR & BSP_GPIO_SAFE_LOW_MASK) != 0U) {
        return 0U;
    }
    for (pin = BSP_GPIO_OPTO_PIN; pin <= BSP_GPIO_BUZZER_PIN; ++pin) {
        if ((BSP_GPIO_SAFE_OUTPUT_MASK & (1UL << pin)) != 0U &&
            ((crl >> BSP_GPIO_CRL_SHIFT(pin)) & BSP_GPIO_MODE_MASK) !=
            BSP_GPIO_OUTPUT_PP_2MHZ) {
            return 0U;
        }
    }
    return 1U;
}

/**
 * @brief 在启动或异常失败时熄灭 WARNING 灯，保留 POWER 灯点亮。
 */
void BSP_Gpio_IndicateFault(void)
{
    GPIOB->BSRR = 1UL << BSP_GPIO_WARNING_LED_PIN;
}
