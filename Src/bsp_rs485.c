#include "bsp_rs485.h"
#include "bsp_board.h"
#include "bsp_gpio.h"
#include "stm32f103xb.h"

#define BSP_RS485_BAUD_RATE                 9600UL
#define BSP_RS485_USART_CR1_REQUIRED        (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE)
#define BSP_RS485_GPIO_MODE_MASK             0xFUL
#define BSP_RS485_GPIO_PA2_AF_PP_50MHZ       0xBUL
#define BSP_RS485_GPIO_PA3_FLOATING_INPUT    0x4UL
#define BSP_RS485_GPIO_PA4_OUTPUT_PP_2MHZ    0x2UL

/**
 * @brief 用寄存器配置 PA2/PA3/PA4 和 USART2 的 9600 8N1。
 * @return 配置读回正确返回 1，否则返回 0。
 */
uint8_t BSP_RS485_Init(void)
{
    uint32_t baud_divider; // USART2 波特率分频值，基于 32 MHz 的 APB1 时钟计算。
    uint32_t crl; // GPIOA 低八个引脚的新配置，保留其他引脚模式。
    uint32_t pin_mask; // PA2、PA3、PA4 配置字段的组合掩码。

    if (BSP_BOARD_PCLK1_HZ < BSP_RS485_BAUD_RATE) {
        return 0U;
    }
    baud_divider = (BSP_BOARD_PCLK1_HZ + (BSP_RS485_BAUD_RATE / 2UL)) /
                   BSP_RS485_BAUD_RATE;
    if (baud_divider == 0U || baud_divider > 0xFFFFUL) {
        return 0U;
    }

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB2ENR;
    (void)RCC->APB1ENR;

    /* USART 空闲时 TX 为高，RS485 收发器方向脚为低表示接收。 */
    GPIOA->BSRR = (1UL << BSP_GPIO_PA_RS485_TX_PIN) |
                  (1UL << (BSP_GPIO_PA_RS485_DIR_PIN +
                           BSP_GPIO_BSRR_RESET_SHIFT));
    pin_mask = (BSP_RS485_GPIO_MODE_MASK << (BSP_GPIO_PA_RS485_TX_PIN * 4U)) |
               (BSP_RS485_GPIO_MODE_MASK << (BSP_GPIO_PA_RS485_RX_PIN * 4U)) |
               (BSP_RS485_GPIO_MODE_MASK << (BSP_GPIO_PA_RS485_DIR_PIN * 4U));
    crl = GPIOA->CRL & ~pin_mask;
    crl |= (BSP_RS485_GPIO_PA2_AF_PP_50MHZ <<
            (BSP_GPIO_PA_RS485_TX_PIN * 4U)) |
           (BSP_RS485_GPIO_PA3_FLOATING_INPUT <<
            (BSP_GPIO_PA_RS485_RX_PIN * 4U)) |
           (BSP_RS485_GPIO_PA4_OUTPUT_PP_2MHZ <<
            (BSP_GPIO_PA_RS485_DIR_PIN * 4U));
    GPIOA->CRL = crl;

    /* 保持 8 数据位、无校验、1 停止位；本阶段不启用 RX 中断。 */
    USART2->CR1 = 0U;
    USART2->CR2 = 0U;
    USART2->CR3 = 0U;
    USART2->BRR = baud_divider;
    USART2->CR1 = BSP_RS485_USART_CR1_REQUIRED;

    if (USART2->BRR != baud_divider ||
        (USART2->CR1 & (BSP_RS485_USART_CR1_REQUIRED | USART_CR1_RXNEIE)) !=
            BSP_RS485_USART_CR1_REQUIRED ||
        ((GPIOA->CRL >> (BSP_GPIO_PA_RS485_TX_PIN * 4U)) &
         BSP_RS485_GPIO_MODE_MASK) != BSP_RS485_GPIO_PA2_AF_PP_50MHZ ||
        ((GPIOA->CRL >> (BSP_GPIO_PA_RS485_RX_PIN * 4U)) &
         BSP_RS485_GPIO_MODE_MASK) != BSP_RS485_GPIO_PA3_FLOATING_INPUT ||
        ((GPIOA->CRL >> (BSP_GPIO_PA_RS485_DIR_PIN * 4U)) &
         BSP_RS485_GPIO_MODE_MASK) != BSP_RS485_GPIO_PA4_OUTPUT_PP_2MHZ ||
        (GPIOA->ODR & (1UL << BSP_GPIO_PA_RS485_DIR_PIN)) != 0U) {
        return 0U;
    }
    return 1U;
}
