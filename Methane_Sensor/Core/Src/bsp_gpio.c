#include "bsp_gpio.h"
#include "stm32f103xb.h"
#include <stdint.h>

/**
 * @brief 使用 CRL 或 CRH 配置一个 STM32F1 GPIO 引脚。
 * @param port 要修改配置寄存器的 GPIO 外设。
 * @param pin 所选 GPIO 端口内的引脚编号。
 * @param mode STM32F1 GPIO 的四位模式与配置值。
 */
static void configure_pin(GPIO_TypeDef *port, uint8_t pin, uint32_t mode)
{
    volatile uint32_t *config = pin < BSP_GPIO_PINS_PER_CONFIG_REGISTER ? &port->CRL : &port->CRH;
    uint32_t shift = (uint32_t)(pin & BSP_GPIO_PIN_CONFIG_INDEX_MASK) * BSP_GPIO_CONFIG_BITS_PER_PIN;
    *config = (*config & ~(BSP_GPIO_CONFIG_FIELD_MASK << shift)) | (mode << shift);
}

/**
 * @brief 检查 GPIO 引脚当前配置是否符合指定的 STM32F1 模式。
 * @param port 包含目标引脚的 GPIO 外设。
 * @param pin 所选 GPIO 端口内的引脚编号。
 * @param mode 期望的 STM32F1 GPIO 四位模式与配置值。
 * @return 配置匹配时返回 1，否则返回 0。
 */
static uint8_t pin_has_mode(GPIO_TypeDef *port, uint8_t pin, uint32_t mode)
{
    volatile uint32_t *config = pin < BSP_GPIO_PINS_PER_CONFIG_REGISTER ? &port->CRL : &port->CRH;
    uint32_t shift = (uint32_t)(pin & BSP_GPIO_PIN_CONFIG_INDEX_MASK) * BSP_GPIO_CONFIG_BITS_PER_PIN;
    return ((*config >> shift) & BSP_GPIO_CONFIG_FIELD_MASK) == mode ? 1U : 0U;
}

/**
 * @brief 初始化并检查板级 GPIO 引脚配置。
 * @return 所需引脚、PA8 红外输入上拉和安全输出电平均配置成功时返回 0，否则返回 -1。
 */
int BSP_Gpio_Init(void)
{
    uint8_t pin;

    /* 先使能 AFIO 与端口时钟，读回寄存器以确保后续配置已经生效。 */
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN;
    (void)RCC->APB2ENR;

    /* 关闭 JTAG 释放 PA15、PB3、PB4，同时保留 PA13/PA14 的 SWD 调试。 */
    AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;

    /* PC817 反相：PA1 高使 24V_IO_OUT 处于关闭低电平；其他执行器和 RS485 方向预装低电平。 */
    GPIOA->BSRR = BSP_GPIO_PA_SAFE_HIGH_MASK |
                  (BSP_GPIO_PA_SAFE_LOW_MASK << BSP_GPIO_BSRR_RESET_SHIFT);
    GPIOB->BSRR = BSP_GPIO_PB_SAFE_LOW_MASK << BSP_GPIO_BSRR_RESET_SHIFT;

    /* STM32F1 通过 ODR=1 选择输入上拉；为 IRM-3638T 开集电极输出建立空闲高电平。 */
    GPIOA->BSRR = BSP_GPIO_PA_IR_INPUT_MASK;

    /* 未启用的板级信号保持浮空输入，不提前实现尚未确认的 RS485 功能。 */
    for (pin = BSP_GPIO_PA_UNUSED_FIRST_PIN; pin <= BSP_GPIO_PA_UNUSED_LAST_PIN; ++pin) {
        configure_pin(GPIOA, pin, BSP_GPIO_MODE_INPUT_FLOATING);
    }
    for (pin = BSP_GPIO_PB_UNUSED_FIRST_PIN; pin <= BSP_GPIO_PB_UNUSED_LAST_PIN; ++pin) {
        configure_pin(GPIOB, pin, BSP_GPIO_MODE_INPUT_FLOATING);
    }

    /* PA1 控制 PC817 输入，PA5 保留为继电器驱动，PA6 控制蜂鸣器。 */
    configure_pin(GPIOA, BSP_GPIO_PA_OPTOCOUPLER_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ);
    configure_pin(GPIOA, BSP_GPIO_PA_RELAY_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ);
    configure_pin(GPIOA, BSP_GPIO_PA_BUZZER_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ);
    configure_pin(GPIOA, BSP_GPIO_PA_RS485_TX_PIN, BSP_GPIO_MODE_AF_PP_2MHZ);
    configure_pin(GPIOA, BSP_GPIO_PA_RS485_RX_PIN, BSP_GPIO_MODE_INPUT_FLOATING);
    configure_pin(GPIOA, BSP_GPIO_PA_RS485_DIRECTION_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ);
    configure_pin(GPIOA, BSP_GPIO_PA_DISPLAY_DIGIT_1_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ);
    configure_pin(GPIOA, BSP_GPIO_PA_DISPLAY_DIGIT_2_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ);
    configure_pin(GPIOA, BSP_GPIO_PA_DISPLAY_DIGIT_3_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ);
    configure_pin(GPIOA, BSP_GPIO_PA_DISPLAY_DIGIT_4_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ);
    configure_pin(GPIOA, BSP_GPIO_PA_DISPLAY_DP_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ);
    for (pin = BSP_GPIO_PB_DISPLAY_SEGMENT_G_PIN; pin <= BSP_GPIO_PB_DISPLAY_SEGMENT_A_PIN; ++pin) {
        configure_pin(GPIOB, pin, BSP_GPIO_MODE_OUTPUT_PP_2MHZ);
    }
    configure_pin(GPIOB, BSP_GPIO_PB_POWER_LED_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ);
    configure_pin(GPIOB, BSP_GPIO_PB_WARNING_LED_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ);

    /* PA2/PA3 为 RS485 USART2 TX/RX，PA4 同时控制 DE 与 /RE；PB10/PB11 留给甲烷 USART3。 */
    configure_pin(GPIOA, BSP_GPIO_PA_IR_INPUT_PIN, BSP_GPIO_MODE_INPUT_PULLUP);
    AFIO->EXTICR[BSP_GPIO_EXTICR_INDEX_PA8] =
                      (AFIO->EXTICR[BSP_GPIO_EXTICR_INDEX_PA8] & ~AFIO_EXTICR3_EXTI8) |
                      AFIO_EXTICR3_EXTI8_PA;
    configure_pin(GPIOB, BSP_GPIO_PB_METHANE_UART_TX_PIN, BSP_GPIO_MODE_AF_PP_2MHZ);
    configure_pin(GPIOB, BSP_GPIO_PB_METHANE_UART_RX_PIN, BSP_GPIO_MODE_INPUT_FLOATING);

    if ((RCC->APB2ENR & (RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN |
                         RCC_APB2ENR_IOPBEN)) !=
        (RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN) ||
        (GPIOA->ODR & BSP_GPIO_PA_SAFE_LOW_MASK) != 0U ||
        (GPIOA->ODR & BSP_GPIO_PA_SAFE_HIGH_MASK) != BSP_GPIO_PA_SAFE_HIGH_MASK ||
        (GPIOB->ODR & BSP_GPIO_PB_SAFE_LOW_MASK) != 0U ||
        pin_has_mode(GPIOA, BSP_GPIO_PA_UNUSED_FIRST_PIN, BSP_GPIO_MODE_INPUT_FLOATING) == 0U ||
        pin_has_mode(GPIOA, BSP_GPIO_PA_UNUSED_LAST_PIN, BSP_GPIO_MODE_INPUT_FLOATING) == 0U ||
        pin_has_mode(GPIOA, BSP_GPIO_PA_RS485_TX_PIN, BSP_GPIO_MODE_AF_PP_2MHZ) == 0U ||
        pin_has_mode(GPIOA, BSP_GPIO_PA_RS485_RX_PIN, BSP_GPIO_MODE_INPUT_FLOATING) == 0U ||
        pin_has_mode(GPIOA, BSP_GPIO_PA_RS485_DIRECTION_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ) == 0U ||
        (GPIOA->ODR & BSP_GPIO_PA_RS485_DIRECTION_MASK) != 0U ||
        pin_has_mode(GPIOA, BSP_GPIO_PA_OPTOCOUPLER_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ) == 0U ||
        pin_has_mode(GPIOA, BSP_GPIO_PA_RELAY_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ) == 0U ||
        pin_has_mode(GPIOA, BSP_GPIO_PA_IR_INPUT_PIN, BSP_GPIO_MODE_INPUT_PULLUP) == 0U ||
        (GPIOA->ODR & BSP_GPIO_PA_IR_INPUT_MASK) == 0U ||
        pin_has_mode(GPIOA, BSP_GPIO_PA_DISPLAY_DIGIT_1_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ) == 0U ||
        pin_has_mode(GPIOA, BSP_GPIO_PA_DISPLAY_DIGIT_2_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ) == 0U ||
        pin_has_mode(GPIOA, BSP_GPIO_PA_DISPLAY_DIGIT_3_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ) == 0U ||
        pin_has_mode(GPIOA, BSP_GPIO_PA_DISPLAY_DIGIT_4_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ) == 0U ||
        pin_has_mode(GPIOA, BSP_GPIO_PA_DISPLAY_DP_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ) == 0U ||
        pin_has_mode(GPIOB, BSP_GPIO_PB_DISPLAY_SEGMENT_G_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ) == 0U ||
        pin_has_mode(GPIOB, BSP_GPIO_PB_DISPLAY_SEGMENT_A_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ) == 0U ||
        pin_has_mode(GPIOB, BSP_GPIO_PB_METHANE_UART_TX_PIN, BSP_GPIO_MODE_AF_PP_2MHZ) == 0U ||
        pin_has_mode(GPIOB, BSP_GPIO_PB_METHANE_UART_RX_PIN, BSP_GPIO_MODE_INPUT_FLOATING) == 0U ||
        pin_has_mode(GPIOB, BSP_GPIO_PB_UNUSED_FIRST_PIN, BSP_GPIO_MODE_INPUT_FLOATING) == 0U ||
        pin_has_mode(GPIOB, BSP_GPIO_PB_UNUSED_LAST_PIN, BSP_GPIO_MODE_INPUT_FLOATING) == 0U ||
        pin_has_mode(GPIOB, BSP_GPIO_PB_POWER_LED_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ) == 0U ||
        pin_has_mode(GPIOB, BSP_GPIO_PB_WARNING_LED_PIN, BSP_GPIO_MODE_OUTPUT_PP_2MHZ) == 0U ||
        (AFIO->EXTICR[BSP_GPIO_EXTICR_INDEX_PA8] & AFIO_EXTICR3_EXTI8) != AFIO_EXTICR3_EXTI8_PA) {
        return -1;
    }

    return 0;
}
