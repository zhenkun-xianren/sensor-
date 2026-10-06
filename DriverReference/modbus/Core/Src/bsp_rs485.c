/* STM32F103 USART2 平台参考；本次未单独编译，硬件未验证。 */
#include "bsp_rs485.h"
#include "bsp_gpio.h"
#include "stm32f103xb.h"
/* 来源参考工程已有 HAL 时基依赖；仅保留 HAL_GetTick，目标工程另用 BSP 时基。 */
#include "stm32f1xx_hal.h"
#if DEV_MODBUS_ENABLE_RESPONSE
#include "bsp_byte_ring.h"
static BspByteRing s_rx; // 保存接收中断与任务共享的字节队列。
static volatile uint8_t s_rx_error; // 保存接收硬件或环形缓冲区错误。
#endif
static uint8_t s_initialized; // 保存 UART 初始化状态，发送超时后清零。

/**
 * @brief 检查 USART2 发送、方向和所选接收功能所需的 GPIO 配置。
 * @return 所需引脚配置匹配且方向输出为低时返回 1，否则返回 0。
 */
static uint8_t rs485_pins_are_ready(void)
{
    uint32_t pa2Shift = BSP_GPIO_CONFIG_BITS_PER_PIN * BSP_GPIO_PA_RS485_TX_PIN; // 保存 TX 配置字段偏移。
    uint32_t pa4Shift = BSP_GPIO_CONFIG_BITS_PER_PIN * BSP_RS485_DIR_PIN; // 保存方向配置字段偏移。
    uint32_t pa2Mode = (GPIOA->CRL >> pa2Shift) & BSP_GPIO_CONFIG_FIELD_MASK; // 读取 TX 引脚模式。
    uint32_t pa4Mode = (GPIOA->CRL >> pa4Shift) & BSP_GPIO_CONFIG_FIELD_MASK; // 读取方向引脚模式。
#if DEV_MODBUS_ENABLE_RESPONSE
    uint32_t pa3Shift = BSP_GPIO_CONFIG_BITS_PER_PIN * BSP_GPIO_PA_RS485_RX_PIN; // 保存 RX 配置字段偏移。
    uint32_t pa3Mode = (GPIOA->CRL >> pa3Shift) & BSP_GPIO_CONFIG_FIELD_MASK; // 读取 RX 引脚模式。
    if (pa3Mode != BSP_GPIO_MODE_INPUT_FLOATING) {
        return 0U;
    }
#endif
    return (pa2Mode == BSP_GPIO_MODE_AF_PP_2MHZ &&
            pa4Mode == BSP_GPIO_MODE_OUTPUT_PP_2MHZ &&
            (GPIOA->ODR & BSP_RS485_DIR_MASK) == 0U) ? 1U : 0U;
}

/**
 * @brief 等待 USART2 状态标志，并用来源工程时基限制整体等待期限。
 * @param flag 状态寄存器中的目标位掩码。
 * @param setValue 期望置位时传非零值，期望清零时传零。
 * @param startTick 整体等待起点的毫秒计数。
 * @param timeoutMs 允许的最长等待毫秒数。
 * @return 状态满足返回 1，超时返回 0。
 */
static uint8_t rs485_wait_status(uint32_t flag, uint8_t setValue,
                                 uint32_t startTick, uint32_t timeoutMs)
{
    for (;;) {
        uint8_t isSet = (USART2->SR & flag) != 0U ? 1U : 0U; // 保存当前目标状态。
        if (isSet == (setValue != 0U ? 1U : 0U)) {
            return 1U;
        }
        if ((uint32_t)(HAL_GetTick() - startTick) >= timeoutMs) {
            return 0U;
        }
    }
}

/**
 * @brief 设置同时连接 DE 和 /RE 的 PA4 方向电平。
 * @param transmit 发送传非零值，释放总线传零。
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
 * @brief 沿用来源 USART2 的 9600/8N1 配置，按功能宏启用接收和中断。
 * @return 成功返回 OK，引脚配置无效返回 INVALID。
 */
BspRs485Status BSP_Rs485_Init(void)
{
    s_initialized = 0U;
    if (rs485_pins_are_ready() == 0U) {
        return BSP_RS485_INVALID;
    }
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR;
#if DEV_MODBUS_ENABLE_RESPONSE
    BSP_ByteRing_Init(&s_rx);
    s_rx_error = 0U;
#endif
    rs485_set_direction(0U);
    USART2->CR1 = 0U;
    USART2->CR2 = 0U;
    USART2->CR3 = 0U;
    USART2->BRR = BSP_RS485_UART_BRR_VALUE;
    /* 保留来源 SR/DR 读取顺序，清除遗留接收状态。 */
    (void)USART2->SR;
    (void)USART2->DR;
#if DEV_MODBUS_ENABLE_RESPONSE
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_SetPriority(USART2_IRQn, BSP_RS485_IRQ_PRIORITY);
    NVIC_EnableIRQ(USART2_IRQn);
#else
    /* 不启用 RE、接收中断或接收队列；TX 与方向由来源 GPIO 适配准备。 */
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE;
#endif
    s_initialized = 1U;
    return BSP_RS485_OK;
}

/**
 * @brief 沿用 TXE/TC 发送流程，超时释放方向并关闭 UART，要求下次先初始化。
 * @param data 非空发送序列。
 * @param length 请求帧字节数，范围为 1 至 255。
 * @return 完整发送返回 OK，失败返回 INVALID、NOT_INITIALIZED 或 TIMEOUT。
 */
BspRs485Status BSP_Rs485_Send(const uint8_t *data, uint8_t length)
{
    uint8_t index; // 遍历发送字节。
    uint32_t startTick; // 保存完整发送共用的超时起点。
    if (data == 0 || length == 0U) {
        return BSP_RS485_INVALID;
    }
    if (s_initialized == 0U) {
        return BSP_RS485_NOT_INITIALIZED;
    }
    startTick = HAL_GetTick();
    rs485_set_direction(1U);
    for (index = 0U; index < length; ++index) {
        if (rs485_wait_status(USART_SR_TXE, 1U, startTick,
                              BSP_RS485_TX_TIMEOUT_MS) == 0U) {
            rs485_set_direction(0U);
            USART2->CR1 = 0U;
            s_initialized = 0U;
            return BSP_RS485_TIMEOUT;
        }
        USART2->DR = data[index];
    }
    if (rs485_wait_status(USART_SR_TC, 1U, startTick,
                          BSP_RS485_TX_TIMEOUT_MS) == 0U) {
        rs485_set_direction(0U);
        USART2->CR1 = 0U;
        s_initialized = 0U;
        return BSP_RS485_TIMEOUT;
    }
    /* TC 表示最后停止位已发出，此时可以释放总线。 */
    rs485_set_direction(0U);
    return BSP_RS485_OK;
}

#if DEV_MODBUS_ENABLE_RESPONSE
/** @brief 在保存、恢复中断屏蔽状态的短临界区清除遗留接收数据。 */
void BSP_Rs485_ClearRx(void)
{
    uint32_t primask; // 保存进入临界区前的中断屏蔽状态。
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
 * @brief 在给定期限内读取接收队列，保留来源等待中断方式。
 * @param data 保存接收字节的非空输出指针。
 * @param timeoutMs 本次读取允许等待的最长毫秒数。
 * @return 成功返回 OK，参数、未初始化、超时或接收错误返回对应状态。
 */
BspRs485Status BSP_Rs485_ReadByte(uint8_t *data, uint32_t timeoutMs)
{
    uint32_t startTick; // 保存本次字节读取的期限起点。
    if (data == 0) {
        return BSP_RS485_INVALID;
    }
    if (s_initialized == 0U) {
        return BSP_RS485_NOT_INITIALIZED;
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
 * @brief 读取来源参考工程的 HAL 系统毫秒时基。
 * @return 毫秒计数，允许自然回绕。
 */
uint32_t BSP_Rs485_GetTickMs(void)
{
    return HAL_GetTick();
}

/** @brief 将 USART2 接收字节写入环形缓冲区并记录硬件或队列错误。 */
void BSP_Rs485_IrqHandler(void)
{
    uint32_t status = USART2->SR; // 保存一次状态读取，按来源顺序随后读取 DR。
    if ((status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U) {
        uint8_t byte = (uint8_t)USART2->DR; // 保存当前接收字节并清除对应硬件状态。
        if ((status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U) {
            s_rx_error = 1U;
        } else if (BSP_ByteRing_Push(&s_rx, byte) == 0U) {
            s_rx_error = 1U;
        }
    }
}
#endif
