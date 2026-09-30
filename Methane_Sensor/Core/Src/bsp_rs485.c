#include "bsp_rs485.h"
#include "bsp_byte_ring.h"
#include "bsp_gpio.h"
#include "stm32f103xb.h"
#include "stm32f1xx_hal.h"

static BspByteRing s_rx;
static volatile uint8_t s_rx_error;
static uint8_t s_initialized;

/**
 * @brief 检查 RS485 所需 PA2、PA3、PA4 的 GPIO 配置。
 * @return PA2 为复用推挽、PA3 为浮空输入且 PA4 为低电平推挽输出时返回 1，否则返回 0。
 */
static uint8_t rs485_pins_are_ready(void)
{
    uint32_t pa2Shift = BSP_GPIO_CONFIG_BITS_PER_PIN * BSP_GPIO_PA_RS485_TX_PIN;
    uint32_t pa3Shift = BSP_GPIO_CONFIG_BITS_PER_PIN * BSP_GPIO_PA_RS485_RX_PIN;
    uint32_t pa4Shift = BSP_GPIO_CONFIG_BITS_PER_PIN * BSP_RS485_DIR_PIN;
    uint32_t pa2Mode = (GPIOA->CRL >> pa2Shift) & BSP_GPIO_CONFIG_FIELD_MASK;
    uint32_t pa3Mode = (GPIOA->CRL >> pa3Shift) & BSP_GPIO_CONFIG_FIELD_MASK;
    uint32_t pa4Mode = (GPIOA->CRL >> pa4Shift) & BSP_GPIO_CONFIG_FIELD_MASK;

    return (pa2Mode == BSP_GPIO_MODE_AF_PP_2MHZ &&
            pa3Mode == BSP_GPIO_MODE_INPUT_FLOATING &&
            pa4Mode == BSP_GPIO_MODE_OUTPUT_PP_2MHZ &&
            (GPIOA->ODR & BSP_RS485_DIR_MASK) == 0U) ? 1U : 0U;
}

/**
 * @brief 等待 USART2 状态寄存器出现指定标志并限制最长等待时间。
 * @param flag USART2 状态寄存器中的目标状态掩码。
 * @param setValue 目标标志期望置位时传入非零值，期望清零时传入零。
 * @param startTick 开始等待时的 HAL 毫秒计数。
 * @param timeoutMs 允许的最长等待时间，单位为毫秒。
 * @return 状态满足条件返回 1，等待超时返回 0。
 */
static uint8_t rs485_wait_status(uint32_t flag, uint8_t setValue,
                                 uint32_t startTick, uint32_t timeoutMs)
{
    for (;;) {
        uint8_t isSet = (USART2->SR & flag) != 0U ? 1U : 0U;
        if (isSet == (setValue != 0U ? 1U : 0U)) {
            return 1U;
        }
        if ((uint32_t)(HAL_GetTick() - startTick) >= timeoutMs) {
            return 0U;
        }
    }
}

/**
 * @brief 设置 PA4 方向控制电平；该引脚同时连接 ADM3485 的 DE 与 /RE。
 * @param transmit 发送方向传入非零值，接收方向传入零。
 */
static void rs485_set_direction(uint8_t transmit)
{
    if (transmit != 0U) {
        GPIOA->BSRR = BSP_RS485_DIR_MASK;
    } else {
        GPIOA->BSRR = BSP_RS485_DIR_MASK << BSP_GPIO_BSRR_RESET_SHIFT;
    }
}

/**
 * @brief 初始化 USART2 为 9600 bit/s、8N1，并使能接收中断。
 * @return 初始化成功返回 BSP_RS485_OK；引脚配置无效时返回 BSP_RS485_INVALID。
 */
BspRs485Status BSP_Rs485_Init(void)
{
    if (rs485_pins_are_ready() == 0U) {
        return BSP_RS485_INVALID;
    }

    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR;
    BSP_ByteRing_Init(&s_rx);
    s_rx_error = 0U;
    s_initialized = 0U;
    rs485_set_direction(0U);

    USART2->CR1 = 0U;
    USART2->CR2 = 0U;
    USART2->CR3 = 0U;
    USART2->BRR = BSP_RS485_UART_BRR_VALUE;
    (void)USART2->SR;
    (void)USART2->DR;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    NVIC_SetPriority(USART2_IRQn, BSP_RS485_IRQ_PRIORITY);
    NVIC_EnableIRQ(USART2_IRQn);
    s_initialized = 1U;

    return BSP_RS485_OK;
}

/**
 * @brief 通过 USART2 发送数据并在发送完成后切回接收方向。
 * @param data 待发送的字节序列。
 * @param length 待发送的字节数。
 * @return 发送成功返回 BSP_RS485_OK；参数错误或硬件等待超时返回对应状态。
 */
BspRs485Status BSP_Rs485_Send(const uint8_t *data, uint8_t length)
{
    uint8_t index;
    uint32_t startTick;

    if (s_initialized == 0U || data == 0 || length == 0U) {
        return BSP_RS485_INVALID;
    }

    startTick = HAL_GetTick();
    rs485_set_direction(1U);
    for (index = 0U; index < length; ++index) {
        if (rs485_wait_status(USART_SR_TXE, 1U, startTick,
                              BSP_RS485_TX_TIMEOUT_MS) == 0U) {
            rs485_set_direction(0U);
            return BSP_RS485_TIMEOUT;
        }
        USART2->DR = data[index];
    }

    if (rs485_wait_status(USART_SR_TC, 1U, startTick,
                          BSP_RS485_TX_TIMEOUT_MS) == 0U) {
        rs485_set_direction(0U);
        return BSP_RS485_TIMEOUT;
    }

    /* USART TC 表示最后一个停止位已发出，此时释放总线并重新启用接收器。 */
    rs485_set_direction(0U);
    return BSP_RS485_OK;
}

/**
 * @brief 丢弃上一事务遗留的接收数据和硬件接收错误。
 */
void BSP_Rs485_ClearRx(void)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    BSP_ByteRing_Init(&s_rx);
    s_rx_error = 0U;
    (void)USART2->SR;
    (void)USART2->DR;
    if (primask == 0U) {
        __enable_irq();
    }
}

/**
 * @brief 在给定时限内从 USART2 接收环形缓冲区读取一个字节。
 * @param data 用于保存接收字节的输出指针。
 * @param timeoutMs 本次读取允许等待的最长时间，单位为毫秒。
 * @return 读取成功返回 BSP_RS485_OK；参数错误、等待超时或接收错误时返回对应状态。
 */
BspRs485Status BSP_Rs485_ReadByte(uint8_t *data, uint32_t timeoutMs)
{
    uint32_t startTick;

    if (s_initialized == 0U || data == 0) {
        return BSP_RS485_INVALID;
    }

    startTick = HAL_GetTick();
    for (;;) {
        if (s_rx_error != 0U) {
            return BSP_RS485_RX_ERROR;
        }
        if (BSP_ByteRing_Pop(&s_rx, data) != 0U) {
            return BSP_RS485_OK;
        }
        if ((uint32_t)(HAL_GetTick() - startTick) >= timeoutMs) {
            return BSP_RS485_TIMEOUT;
        }
        __WFI();
    }
}

/**
 * @brief 读取 HAL 系统毫秒时基以供协议层计算整体应答期限。
 * @return 自 HAL 初始化以来经过的毫秒计数，允许自然回绕。
 */
uint32_t BSP_Rs485_GetTickMs(void)
{
    return HAL_GetTick();
}

/**
 * @brief 将 USART2 接收字节写入环形缓冲区并记录硬件错误。
 */
void BSP_Rs485_IrqHandler(void)
{
    uint32_t status = USART2->SR;

    if ((status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U) {
        uint8_t byte = (uint8_t)USART2->DR;
        if ((status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U) {
            s_rx_error = 1U;
        } else if (BSP_ByteRing_Push(&s_rx, byte) == 0U) {
            s_rx_error = 1U;
        }
    }
}
