#include "bsp_uart.h"
#include "bsp_byte_ring.h"
#include <stdbool.h>
#include <stddef.h>

static BspByteRing s_rx; // 保存 USART2 接收字节，与 USART3 的队列完全独立。
static BspByteRing s_tx; // 保存 USART2 待发送字节，环形队列有效容量为 127 字节。
static volatile bool s_initialized = false; // 保存串口初始化结果，供任务和中断共同观察。
static volatile bool s_rx_error = false; // 合并尚未被任务消费的接收错误通知，不清除已有有效字节。
volatile uint32_t g_uart_rx_byte_count; // 累计进入接收队列的有效字节数，自然回绕。
volatile uint32_t g_uart_tx_load_count; // 累计写入发送数据寄存器的字节数，自然回绕。
volatile uint32_t g_uart_parity_error_count; // 累计奇偶错误次数，自然回绕。
volatile uint32_t g_uart_frame_error_count; // 累计帧错误次数，自然回绕。
volatile uint32_t g_uart_noise_error_count; // 累计噪声错误次数，自然回绕。
volatile uint32_t g_uart_hardware_overrun_count; // 累计硬件溢出事件次数，自然回绕。
volatile uint32_t g_uart_rx_queue_overrun_count; // 累计接收队列满时丢弃的新字节数，自然回绕。

/**
 * @brief 配置 PA2/PA3 及 USART2 的 9600/8N1 中断收发，不覆盖其他外设和 SWD。
 * @return 成功返回 OK；中断上下文返回 ARGUMENT；时钟、引脚或读回失败返回 IO。
 */
DevUartStatus BSP_Uart_Init(void)
{
    uint32_t saved; // 保存调用前中断屏蔽状态，所有出口原样恢复。
    uint32_t mapr; // 保存除 USART2 和只写 SWJ 字段外的既有复用配置。
    if (__get_IPSR() != 0U) return DEV_UART_ERROR_ARGUMENT;
    if (BSP_Board_CheckClock() != BSP_BOARD_OK) return DEV_UART_ERROR_IO;
    saved = __get_PRIMASK();
    __disable_irq();
    s_initialized = false;
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB2ENR;
    (void)RCC->APB1ENR;
    NVIC_DisableIRQ(USART2_IRQn);
    USART2->CR1 = BSP_UART_DISABLED;
    if ((GPIOA->LCKR & GPIO_LCKR_LCKK) != 0U &&
        (GPIOA->LCKR & BSP_UART_PIN_MASK) != 0U) {
        __set_PRIMASK(saved);
        return DEV_UART_ERROR_IO;
    }
    GPIOA->BSRR = GPIO_BSRR_BS2; // 先预置 TX 空闲高电平，再切换为复用输出。
    GPIOA->CRL = (GPIOA->CRL & ~BSP_UART_PIN_FIELDS) | BSP_UART_PIN_MODES;
    mapr = AFIO->MAPR & ~(AFIO_MAPR_USART2_REMAP | AFIO_MAPR_SWJ_CFG);
    /* RM0008 p184：SWJ_CFG 读值未定义，写 MAPR 时显式关闭 JTAG 并保留 SWD。 */
    AFIO->MAPR = mapr | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
    USART2->CR2 = BSP_UART_ASYNC_ONE_STOP; // 异步、一个停止位，不占用 PA4 时钟脚。
    USART2->CR3 = BSP_UART_DISABLED; // 不使用 DMA、单线模式和 RTS/CTS 流控。
    USART2->BRR = BSP_UART_BRR_VALUE;
    BSP_ByteRing_Init(&s_rx);
    BSP_ByteRing_Init(&s_tx);
    s_rx_error = false;
    (void)USART2->SR;
    (void)USART2->DR; // RM0008 p818-819：先读 SR 再读 DR，清除启动遗留状态。
    USART2->CR1 = BSP_UART_CONTROL_FLAGS; // M/PCE 清零：八位数据，无校验；TXEIE 按入队启用。
    __DSB();
    if ((GPIOA->CRL & BSP_UART_PIN_FIELDS) != BSP_UART_PIN_MODES ||
        (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG) != mapr ||
        USART2->BRR != BSP_UART_BRR_VALUE || USART2->CR1 != BSP_UART_CONTROL_FLAGS ||
        USART2->CR2 != BSP_UART_ASYNC_ONE_STOP || USART2->CR3 != BSP_UART_DISABLED) {
        USART2->CR1 = BSP_UART_DISABLED;
        __set_PRIMASK(saved);
        return DEV_UART_ERROR_IO;
    }
    NVIC_SetPriority(USART2_IRQn, BSP_UART_IRQ_PRIORITY);
    NVIC_ClearPendingIRQ(USART2_IRQn);
    s_initialized = true;
    NVIC_EnableIRQ(USART2_IRQn);
    __set_PRIMASK(saved);
    return DEV_UART_OK;
}

/**
 * @brief 非阻塞消费接收错误通知或取出队首字节，临界区不等待硬件。
 * @param data 有效字节输出位置，仅 OK 时写入。
 * @return 返回 OK、EMPTY、ERROR_RX、ARGUMENT、NOT_INITIALIZED 或 IO。
 */
DevUartStatus BSP_Uart_ReadByte(uint8_t *data)
{
    uint32_t saved; // 保存操作前中断状态，保护任务与接收中断的队列共享。
    DevUartStatus result; // 保存本次读取结果，不把错误或空队列伪装为字节。
    if (data == NULL || __get_IPSR() != 0U) return DEV_UART_ERROR_ARGUMENT;
    saved = __get_PRIMASK();
    __disable_irq();
    if (!s_initialized) result = DEV_UART_NOT_INITIALIZED;
    else if ((USART2->CR1 & BSP_UART_CONTROL_FLAGS) != BSP_UART_CONTROL_FLAGS)
        result = DEV_UART_ERROR_IO;
    else if (s_rx_error) {
        s_rx_error = false;
        result = DEV_UART_ERROR_RX;
    } else {
        result = BSP_ByteRing_Pop(&s_rx, data) != BSP_UART_RING_EMPTY_OR_FULL
            ? DEV_UART_OK : DEV_UART_EMPTY;
    }
    __set_PRIMASK(saved);
    return result;
}

/**
 * @brief 非阻塞入队并启用发送中断，队列满时不消费调用方字节。
 * @param data 要原样发送的八位数据。
 * @return 返回 OK、BUSY、ARGUMENT、NOT_INITIALIZED 或 IO；OK 不表示停止位已发送。
 */
DevUartStatus BSP_Uart_WriteByte(uint8_t data)
{
    uint32_t saved; // 保存操作前中断状态，防止队列入队与 TXEIE 关闭之间发生竞态。
    DevUartStatus result; // 保存实际入队结果。
    if (__get_IPSR() != 0U) return DEV_UART_ERROR_ARGUMENT;
    saved = __get_PRIMASK();
    __disable_irq();
    if (!s_initialized) result = DEV_UART_NOT_INITIALIZED;
    else if ((USART2->CR1 & BSP_UART_CONTROL_FLAGS) != BSP_UART_CONTROL_FLAGS)
        result = DEV_UART_ERROR_IO;
    else if (BSP_ByteRing_Push(&s_tx, data) == BSP_UART_RING_EMPTY_OR_FULL)
        result = DEV_UART_BUSY;
    else {
        USART2->CR1 |= USART_CR1_TXEIE;
        result = DEV_UART_OK;
    }
    __set_PRIMASK(saved);
    return result;
}

/** @brief 按 SR/DR 顺序接收并记录错误；TXE 每次发送一个字节，空队列立即关闭发送中断。 */
void BSP_Uart_IrqHandler(void)
{
    uint32_t status; // 保存当前硬件状态快照，读取 DR 前保留接收错误标志。
    uint8_t byte; // 保存当前接收或发送的完整八位数据。
    if (!s_initialized) return;
    status = USART2->SR;
    if ((status & BSP_UART_RX_EVENT_FLAGS) != 0U) {
        byte = (uint8_t)USART2->DR;
        if ((status & USART_SR_PE) != 0U) ++g_uart_parity_error_count;
        if ((status & USART_SR_FE) != 0U) ++g_uart_frame_error_count;
        if ((status & USART_SR_NE) != 0U) ++g_uart_noise_error_count;
        if ((status & USART_SR_ORE) != 0U) ++g_uart_hardware_overrun_count;
        if ((status & BSP_UART_RX_ERROR_FLAGS) != 0U) {
            s_rx_error = true; // 丢弃该错误事件字节，不清空此前已接收的有效队列。
        } else if ((status & USART_SR_RXNE) != 0U) {
            if (BSP_ByteRing_Push(&s_rx, byte) == BSP_UART_RING_EMPTY_OR_FULL) {
                ++g_uart_rx_queue_overrun_count;
                s_rx_error = true; // 队列满只丢新字节，不覆盖已排队字节。
            } else ++g_uart_rx_byte_count;
        }
    }
    if ((status & USART_SR_TXE) != 0U && (USART2->CR1 & USART_CR1_TXEIE) != 0U) {
        if (BSP_ByteRing_Pop(&s_tx, &byte) != BSP_UART_RING_EMPTY_OR_FULL) {
            USART2->DR = byte;
            ++g_uart_tx_load_count;
        } else USART2->CR1 &= ~USART_CR1_TXEIE;
    }
}

/** @brief 衔接本工程唯一的 USART2 向量，不调用 RTOS API 或执行阻塞操作。 */
void USART2_IRQHandler(void)
{
    BSP_Uart_IrqHandler();
}
