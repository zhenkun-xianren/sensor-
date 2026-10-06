#ifndef BSP_UART_H
#define BSP_UART_H

#include "dev_uart.h"
#include "bsp_board.h"
#include "stm32f103xb.h"

/* 正式引脚表 V1.0：PA2_TX、PA3_RX；RM0008 Rev21 p186 默认 USART2 映射。 */
#define BSP_UART_PIN_MASK (GPIO_ODR_ODR2 | GPIO_ODR_ODR3)
#define BSP_UART_PIN_FIELDS (GPIO_CRL_MODE2 | GPIO_CRL_CNF2 | GPIO_CRL_MODE3 | GPIO_CRL_CNF3)
/* RM0008 GPIO 章节：PA2 为 2MHz 复用推挽，PA3 为浮空输入。 */
#define BSP_UART_PIN_MODES (GPIO_CRL_MODE2_1 | GPIO_CRL_CNF2_1 | GPIO_CRL_CNF3_0)
#define BSP_UART_BAUD_RATE 9600UL
#define BSP_UART_CLOCK_HZ BSP_BOARD_PCLK1_HZ
/* RM0008 p798/p820：16 倍采样时，BRR 编码等于 PCLK1/波特率的最近整数。 */
#define BSP_UART_BRR_VALUE ((BSP_UART_CLOCK_HZ + BSP_UART_BAUD_RATE / 2UL) / BSP_UART_BAUD_RATE)
#define BSP_UART_IRQ_PRIORITY 6U
#define BSP_UART_DISABLED 0UL
#define BSP_UART_ASYNC_ONE_STOP 0UL
#define BSP_UART_CONTROL_FLAGS (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE)
#define BSP_UART_RX_ERROR_FLAGS (USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE)
#define BSP_UART_RX_EVENT_FLAGS (USART_SR_RXNE | USART_SR_FE | USART_SR_NE | USART_SR_ORE)
#define BSP_UART_RING_EMPTY_OR_FULL 0U

extern volatile uint32_t g_uart_rx_byte_count; // 累计进入接收队列的有效字节数。
extern volatile uint32_t g_uart_tx_load_count; // 累计写入发送数据寄存器的字节数，不表示停止位已结束。
extern volatile uint32_t g_uart_parity_error_count; // 累计检测到的奇偶校验错误。
extern volatile uint32_t g_uart_frame_error_count; // 累计检测到的帧错误。
extern volatile uint32_t g_uart_noise_error_count; // 累计检测到的噪声错误。
extern volatile uint32_t g_uart_hardware_overrun_count; // 累计硬件接收溢出事件。
extern volatile uint32_t g_uart_rx_queue_overrun_count; // 累计接收队列满导致的新字节丢弃。

/**
 * @brief 按正式映射初始化 USART2 9600/8N1，并清空独立收发队列。
 * @return 成功返回 OK；中断上下文返回 ARGUMENT；板级配置失败返回 IO。
 */
DevUartStatus BSP_Uart_Init(void);
/**
 * @brief 在短临界区非阻塞取出一个有效字节或消费一次接收错误通知。
 * @param data 字节输出位置，仅返回 OK 时更新。
 * @return 返回 OK、EMPTY、ERROR_RX、ARGUMENT、NOT_INITIALIZED 或 IO。
 */
DevUartStatus BSP_Uart_ReadByte(uint8_t *data);
/**
 * @brief 在短临界区将一个字节入队并打开 TXE 发送中断。
 * @param data 要原样提交的八位数据。
 * @return 入队成功返回 OK，队列满返回 BUSY，其他失败返回 ARGUMENT、NOT_INITIALIZED 或 IO。
 */
DevUartStatus BSP_Uart_WriteByte(uint8_t data);
/** @brief 接收中断入队并统计错误，TXE 中断从发送队列取字节。 */
void BSP_Uart_IrqHandler(void);
/** @brief 提供本工程唯一的 USART2 中断向量实现。 */
void USART2_IRQHandler(void);

#endif /* BSP_UART_H */
