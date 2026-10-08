/* 雷达板STM32F103C8T6：PB5方向，PB6/PB7为USART1重映射。 */
#include "bsp_rs485.h"
#include "stm32f1xx_hal.h"
#include <stddef.h>
#include <stdbool.h>
#if DEV_MODBUS_ENABLE_RESPONSE
#include "bsp_byte_ring.h"
static BspByteRing s_rx; // 接收中断与调用者共享的静态字节队列。
static volatile bool s_rx_error = false; // 是否发生硬件或队列错误。
#endif
static bool s_initialized = false; // 串口是否已初始化，发送超时后清除。
enum {
    RS485_RELEASE = 0, // 方向低电平，释放驱动并启用接收。
    RS485_TRANSMIT = 1, // 方向高电平，驱动总线并关闭本地接收。
    RS485_FLAG_CLEAR = 0, // 期望状态标志清零。
    RS485_FLAG_SET = 1, // 期望状态标志置位。
    RS485_CHECK_FAILED = 0, // 条件未满足。
    RS485_CHECK_PASSED = 1 // 条件已满足。
};
/**
 * @brief 检查USART1重映射、PB5/PB6/PB7配置和方向默认锁存。
 * @return 配置符合契约返回RS485_CHECK_PASSED，否则返回RS485_CHECK_FAILED。
 */
static uint8_t rs485_pins_are_ready(void)
{
    return (GPIOB->CRL & BSP_RS485_GPIO_FIELDS) == BSP_RS485_GPIO_MODES &&
        (GPIOB->ODR & BSP_RS485_DIR_MASK) == 0U &&
        (AFIO->MAPR & AFIO_MAPR_USART1_REMAP) != 0U ?
        RS485_CHECK_PASSED : RS485_CHECK_FAILED;
}
/**
 * @brief 在统一期限内等待USART1状态；保留当前生成工程的HAL毫秒时基。
 * @param flag 状态寄存器目标标志。
 * @param setValue 期望标志是否置位。
 * @param startTick 整体等待起点毫秒数。
 * @param timeoutMs 整体允许等待毫秒数。
 * @return 满足条件返回RS485_CHECK_PASSED，超时返回RS485_CHECK_FAILED。
 */
static uint8_t rs485_wait_status(uint32_t flag, uint8_t setValue,
                                 uint32_t startTick, uint32_t timeoutMs)
{
    for (;;) {
        bool is_set = (USART1->SR & flag) != 0U; // 当前目标状态。
        if (is_set == (setValue != RS485_FLAG_CLEAR)) return RS485_CHECK_PASSED;
        if ((uint32_t)(HAL_GetTick() - startTick) >= timeoutMs)
            return RS485_CHECK_FAILED;
    }
}
/**
 * @brief 写PB5方向锁存；ADM3485的DE与低有效RE共接。
 * @param transmit RS485_TRANSMIT发送，RS485_RELEASE释放总线。
 */
static void rs485_set_direction(uint8_t transmit)
{
    GPIOB->BSRR = transmit == RS485_TRANSMIT ? BSP_RS485_DIR_MASK :
        (BSP_RS485_DIR_MASK << BSP_RS485_RESET_SHIFT);
}
/**
 * @brief 配置USART1重映射并使用库默认9600/8N1，保留其他引脚。
 * @return 成功返回BSP_RS485_OK，时钟或引脚配置异常返回BSP_RS485_INVALID。
 */
BspRs485Status BSP_Rs485_Init(void)
{
    uint32_t irq_state = __get_PRIMASK(); // 保存调用前中断屏蔽状态。
    uint32_t remap; // 保存AFIO共享寄存器的非调试字段。
    s_initialized = false;
    __disable_irq();
    RCC->APB2ENR |= BSP_RS485_GPIO_CLOCKS | RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    NVIC_DisableIRQ(USART1_IRQn);
    USART1->CR1 = 0U;
    rs485_set_direction(RS485_RELEASE);
    /* RM0008：仅改PB5/6/7四位字段，PB8继电器及其他引脚保持。 */
    GPIOB->BSRR = GPIO_BSRR_BS6;
    GPIOB->CRL = (GPIOB->CRL & ~BSP_RS485_GPIO_FIELDS) | BSP_RS485_GPIO_MODES;
    /* SWJ_CFG读值未定义；每次MAPR写入均显式保持SWD并关闭JTAG。 */
    remap = AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG;
    AFIO->MAPR = remap | AFIO_MAPR_SWJ_CFG_JTAGDISABLE | AFIO_MAPR_USART1_REMAP;
    if (rs485_pins_are_ready() != RS485_CHECK_PASSED) {
        __set_PRIMASK(irq_state);
        return BSP_RS485_INVALID;
    }
#if DEV_MODBUS_ENABLE_RESPONSE
    BSP_ByteRing_Init(&s_rx);
    s_rx_error = false;
#endif
    USART1->CR2 = 0U;
    USART1->CR3 = 0U;
    /* USART1取PCLK2=8MHz；按最近整数配置BRR，禁止照搬来源32MHz常数。 */
    USART1->BRR = BSP_RS485_UART_BRR_VALUE;
    (void)USART1->SR;
    (void)USART1->DR;
#if DEV_MODBUS_ENABLE_RESPONSE
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_ClearPendingIRQ(USART1_IRQn);
    NVIC_SetPriority(USART1_IRQn, BSP_RS485_IRQ_PRIORITY);
    NVIC_EnableIRQ(USART1_IRQn);
#else
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE;
#endif
    s_initialized = true;
    __set_PRIMASK(irq_state);
    return BSP_RS485_OK;
}
/**
 * @brief 发送完整字节序列，等待TC后释放方向；超时关闭串口。
 * @param data 非空待发送序列。
 * @param length 待发送字节数，范围1至255。
 * @return 成功返回OK，参数、初始化或超时异常返回对应状态。
 */
BspRs485Status BSP_Rs485_Send(const uint8_t *data, uint8_t length)
{
    uint8_t index; // 遍历待发送字节。
    uint32_t start_tick; // 保存整体发送期限起点。
    if (data == NULL || length == 0U) return BSP_RS485_INVALID;
    if (!s_initialized) return BSP_RS485_NOT_INITIALIZED;
    start_tick = HAL_GetTick();
    rs485_set_direction(RS485_TRANSMIT);
    for (index = 0U; index < length; ++index) {
        if (rs485_wait_status(USART_SR_TXE, RS485_FLAG_SET, start_tick,
                              BSP_RS485_TX_TIMEOUT_MS) != RS485_CHECK_PASSED) {
            rs485_set_direction(RS485_RELEASE);
            USART1->CR1 = 0U;
            s_initialized = false;
            return BSP_RS485_TIMEOUT;
        }
        USART1->DR = data[index];
    }
    if (rs485_wait_status(USART_SR_TC, RS485_FLAG_SET, start_tick,
                          BSP_RS485_TX_TIMEOUT_MS) != RS485_CHECK_PASSED) {
        rs485_set_direction(RS485_RELEASE);
        USART1->CR1 = 0U;
        s_initialized = false;
        return BSP_RS485_TIMEOUT;
    }
    /* TC表示最后停止位完成，此后才能释放驱动。 */
    rs485_set_direction(RS485_RELEASE);
    return BSP_RS485_OK;
}
#if DEV_MODBUS_ENABLE_RESPONSE
/** @brief 在保存恢复中断状态的短临界区清除上一事务的接收状态。 */
void BSP_Rs485_ClearRx(void)
{
    uint32_t irq_state = __get_PRIMASK(); // 保存原中断状态。
    __disable_irq();
    BSP_ByteRing_Init(&s_rx);
    s_rx_error = false;
    (void)USART1->SR;
    (void)USART1->DR;
    __set_PRIMASK(irq_state);
}
/**
 * @brief 在指定期限内从静态接收队列取得一个字节。
 * @param data 接收字节的非空输出指针。
 * @param timeoutMs 允许等待的毫秒数。
 * @return 成功返回OK，参数、未初始化、接收错误或超时返回对应状态。
 */
BspRs485Status BSP_Rs485_ReadByte(uint8_t *data, uint32_t timeoutMs)
{
    uint32_t start_tick; // 保存本次读取期限起点。
    if (data == NULL) return BSP_RS485_INVALID;
    if (!s_initialized) return BSP_RS485_NOT_INITIALIZED;
    start_tick = HAL_GetTick();
    for (;;) {
        if (s_rx_error) return BSP_RS485_RX_ERROR;
        if (BSP_ByteRing_Pop(&s_rx, data) != 0U) return BSP_RS485_OK;
        if ((uint32_t)(HAL_GetTick() - start_tick) >= timeoutMs) return BSP_RS485_TIMEOUT;
        __WFI();
    }
}
/**
 * @brief 读取当前生成工程已有的HAL毫秒时基；只保留计时依赖。
 * @return 可自然回绕的毫秒计数。
 */
uint32_t BSP_Rs485_GetTickMs(void)
{
    return HAL_GetTick();
}
/** @brief 按SR后DR读取顺序处理USART1接收中断，并记录队列或硬件错误。 */
void BSP_Rs485_IrqHandler(void)
{
    uint32_t status = USART1->SR; // 保存读DR之前的串口状态。
    if ((status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U) {
        uint8_t byte = (uint8_t)USART1->DR; // 保存接收字节并清除对应硬件标志。
        if ((status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U)
            s_rx_error = true;
        else if (BSP_ByteRing_Push(&s_rx, byte) == 0U)
            s_rx_error = true;
    }
}
#endif
