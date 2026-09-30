#include "bsp_uart3.h"
#include "bsp_byte_ring.h"
#include "bsp_gpio.h"
#include "stm32f103xb.h"

static BspByteRing s_rx;
static volatile uint8_t s_overrun;

/**
 * @brief 将 USART3 初始化为甲烷传感器使用的 9600 bit/s、8N1 串口。
 */
void BSP_Uart3_Init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
    BSP_ByteRing_Init(&s_rx);
    s_overrun = 0U;
    USART3->CR1 = 0U;
    USART3->BRR = BSP_UART3_BRR_VALUE; /* PCLK1 36 MHz / 9600, 8N1. */
    USART3->CR2 = 0U;
    USART3->CR3 = 0U;
    USART3->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_SetPriority(USART3_IRQn, BSP_UART3_IRQ_PRIORITY);
    NVIC_EnableIRQ(USART3_IRQn);
}

/**
 * @brief 发送字节序列，并限制每次硬件就绪轮询的次数。
 * @param data 待发送的字节序列。
 * @param length 序列中的字节数。
 * @return 所有字节发送成功时返回 1，数据无效或超时时返回 0。
 */
uint8_t BSP_Uart3_Write(const uint8_t *data, uint8_t length)
{
    uint8_t index;
    if (data == 0) return 0U;
    for (index = 0U; index < length; ++index) {
        uint32_t timeout = BSP_UART3_TX_READY_POLL_LIMIT;
        while ((USART3->SR & USART_SR_TXE) == 0U && --timeout != 0U) { }
        if (timeout == 0U) return 0U;
        USART3->DR = data[index];
    }
    return 1U;
}

/**
 * @brief 从 USART3 环形缓冲区中读取一个字节。
 * @param data 有接收数据时用于保存读出的字节。
 * @return 读取到字节时返回 1，否则返回 0。
 */
uint8_t BSP_Uart3_Read(uint8_t *data)
{
    return data != 0 ? BSP_ByteRing_Pop(&s_rx, data) : 0U;
}

/**
 * @brief 原子读取并清除 USART3 接收溢出标志。
 * @return 曾发生接收溢出时返回 1，否则返回 0。
 */
uint8_t BSP_Uart3_TakeOverrun(void)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t result;
    __disable_irq();
    result = s_overrun;
    s_overrun = 0U;
    if (primask == 0U) __enable_irq();
    return result;
}

/**
 * @brief 将 USART3 接收字节写入环形缓冲区并记录硬件错误。
 */
void BSP_Uart3_IrqHandler(void)
{
    uint32_t status = USART3->SR;
    if ((status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U) {
        uint8_t byte = (uint8_t)USART3->DR; /* SR then DR also clears hardware errors. */
        if ((status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U) {
            s_overrun = 1U;
        } else {
            if (BSP_ByteRing_Push(&s_rx, byte) == 0U) s_overrun = 1U;
        }
    }
}
