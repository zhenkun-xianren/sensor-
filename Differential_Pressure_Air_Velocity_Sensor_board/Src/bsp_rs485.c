#include "bsp_rs485.h"
#include "bsp_byte_ring.h"
#include <stdbool.h>
#include <stddef.h>
static BspByteRing s_rx; // 保存接收中断与单调用任务共享的环形队列。
static volatile bool s_rx_error = false; // 保存硬件或队列接收错误。
static bool s_initialized = false; // 保存USART3最近初始化结果。
/** @brief 核对TX/RX/方向及USART3默认重映射配置。
 * @return 正确BSP_BOARD_HIGH，否则BSP_BOARD_LOW。 */
static uint8_t rs485_pins_are_ready(void)
{
    uint32_t fields = GPIO_CRH_MODE10 | GPIO_CRH_CNF10 | GPIO_CRH_MODE11 | GPIO_CRH_CNF11; // TX和RX字段。
    uint32_t modes = GPIO_CRH_MODE10_1 | GPIO_CRH_CNF10_1 | GPIO_CRH_CNF11_0; // 2MHz复用推挽TX和浮空RX。
    return ((GPIOB->CRH & fields) == modes &&
        (GPIOB->CRL & (GPIO_CRL_MODE2 | GPIO_CRL_CNF2)) == GPIO_CRL_MODE2_1 &&
        (GPIOB->ODR & BSP_RS485_DIR_MASK) == 0U && (AFIO->MAPR & AFIO_MAPR_USART3_REMAP) == 0U)
        ? BSP_BOARD_HIGH : BSP_BOARD_LOW;
}
/** @brief 有界等待USART3标志，SysTick提供不依赖事务的毫秒时基。
 * @param flag 要观察的USART状态位。
 * @param setValue 期望置位传高，期望清零传低。
 * @param startTick 整体操作开始毫秒计数。
 * @param timeoutMs 整体操作期限，单位毫秒。
 * @return 达到目标BSP_BOARD_HIGH，超时BSP_BOARD_LOW。 */
static uint8_t rs485_wait_status(uint32_t flag, uint8_t setValue, uint32_t startTick, uint32_t timeoutMs)
{
    for (;;) {
        bool is_set = (USART3->SR & flag) != 0U; // 保存硬件当前状态。
        if (is_set == (setValue != BSP_BOARD_LOW)) return BSP_BOARD_HIGH;
        if ((uint32_t)(BSP_Time_GetMs() - startTick) >= timeoutMs) return BSP_BOARD_LOW;
        __WFI(); // 等待下一次中断；整个过程可被FreeRTOS抢占，不屏蔽时基。
    }
}
/** @brief 控制PB2同时连接的DE和/RE，高发送、低接收。
 * @param transmit 发送传高，释放总线传低。 */
static void rs485_set_direction(uint8_t transmit)
{
    GPIOB->BSRR = transmit != BSP_BOARD_LOW ? BSP_RS485_DIR_MASK :
        (BSP_RS485_DIR_MASK << BSP_BOARD_BSRR_RESET_SHIFT);
}
/** @brief 初始化本板USART3 9600/8N1和完整接收能力。
 * @return 正常BSP_RS485_OK，板级条件不符BSP_RS485_INVALID。 */
BspRs485Status BSP_Rs485_Init(void)
{
    uint32_t saved = __get_PRIMASK(); // 保存初始化前中断状态。
    uint32_t mapr; // 保存需保留的AFIO重映射字段。
    uint32_t fields = GPIO_CRH_MODE10 | GPIO_CRH_CNF10 | GPIO_CRH_MODE11 | GPIO_CRH_CNF11; // USART3引脚字段。
    uint32_t modes = GPIO_CRH_MODE10_1 | GPIO_CRH_CNF10_1 | GPIO_CRH_CNF11_0; // 2MHz复用推挽TX和浮空RX。
    if (BSP_Board_CheckClock() != BSP_BOARD_OK) return BSP_RS485_INVALID;
    __disable_irq();
    s_initialized = false;
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN | RCC_APB2ENR_AFIOEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
    (void)RCC->APB1ENR; (void)RCC->APB2ENR;
    NVIC_DisableIRQ(USART3_IRQn);
    rs485_set_direction(BSP_BOARD_LOW); GPIOB->BSRR = GPIO_BSRR_BS10;
    if ((GPIOB->LCKR & GPIO_LCKR_LCKK) != 0U &&
        (GPIOB->LCKR & (GPIO_ODR_ODR2 | GPIO_ODR_ODR10 | GPIO_ODR_ODR11)) != 0U) {
        USART3->CR1 = 0U; __set_PRIMASK(saved); return BSP_RS485_INVALID;
    }
    GPIOB->CRL = (GPIOB->CRL & ~(GPIO_CRL_MODE2 | GPIO_CRL_CNF2)) | GPIO_CRL_MODE2_1;
    GPIOB->CRH = (GPIOB->CRH & ~fields) | modes;
    mapr = AFIO->MAPR & ~(AFIO_MAPR_USART3_REMAP | AFIO_MAPR_SWJ_CFG);
    AFIO->MAPR = mapr | AFIO_MAPR_SWJ_CFG_JTAGDISABLE; // SWJ_CFG只写，每次MAPR写入显式保留SWD。
    USART3->CR1 = 0U; USART3->CR2 = 0U; USART3->CR3 = 0U;
    BSP_ByteRing_Init(&s_rx); s_rx_error = false;
    USART3->BRR = BSP_RS485_UART_BRR_VALUE;
    (void)USART3->SR; (void)USART3->DR; // 按RM0008读取SR再DR清除遗留状态。
    if (rs485_pins_are_ready() == BSP_BOARD_LOW || USART3->BRR != BSP_RS485_UART_BRR_VALUE) {
        USART3->CR1 = 0U; __set_PRIMASK(saved); return BSP_RS485_INVALID;
    }
    USART3->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_PEIE;
    USART3->CR3 = USART_CR3_EIE;
    NVIC_SetPriority(USART3_IRQn, BSP_RS485_IRQ_PRIORITY); NVIC_ClearPendingIRQ(USART3_IRQn);
    s_initialized = true;
    NVIC_EnableIRQ(USART3_IRQn); __set_PRIMASK(saved); return BSP_RS485_OK;
}
/** @brief 有界发送并等最后停止位结束后释放总线；失败关闭UART。
 * @param data 非空字节序列。
 * @param length 发送字节数，范围1至255。
 * @return 本地发送完成、参数错误、未初始化或超时的明确状态。 */
BspRs485Status BSP_Rs485_Send(const uint8_t *data, uint8_t length)
{
    uint8_t index; // 遍历本次请求帧。
    uint32_t startTick; // 保存整个发送过程共用的期限起点。
    if (data == NULL || length == 0U) return BSP_RS485_INVALID;
    if (!s_initialized) return BSP_RS485_NOT_INITIALIZED;
    if (__get_PRIMASK() != 0U || __get_BASEPRI() != 0U || __get_IPSR() != 0U) return BSP_RS485_INVALID;
    startTick = BSP_Time_GetMs(); rs485_set_direction(BSP_BOARD_HIGH);
    for (index = 0U; index < length; ++index) {
        if (rs485_wait_status(USART_SR_TXE, BSP_BOARD_HIGH, startTick, BSP_RS485_TX_TIMEOUT_MS) == BSP_BOARD_LOW) break;
        USART3->DR = data[index];
    }
    if (index != length || rs485_wait_status(USART_SR_TC, BSP_BOARD_HIGH, startTick, BSP_RS485_TX_TIMEOUT_MS) == BSP_BOARD_LOW) {
        rs485_set_direction(BSP_BOARD_LOW); USART3->CR1 = 0U; s_initialized = false;
        return BSP_RS485_TIMEOUT;
    }
    rs485_set_direction(BSP_BOARD_LOW); return BSP_RS485_OK;
}
/** @brief 在短临界区清除旧接收数据、错误及遗留硬件状态。 */
void BSP_Rs485_ClearRx(void)
{
    uint32_t saved = __get_PRIMASK(); // 保存原中断状态。
    __disable_irq(); BSP_ByteRing_Init(&s_rx); s_rx_error = false;
    (void)USART3->SR; (void)USART3->DR;
    NVIC_ClearPendingIRQ(USART3_IRQn); __set_PRIMASK(saved);
}
/** @brief 在给定期限内读取一个接收字节，硬件及队列错误不伪装成功。
 * @param data 非空字节输出位置。
 * @param timeoutMs 本次等待期限，单位毫秒。
 * @return 返回明确的接收结果。 */
BspRs485Status BSP_Rs485_ReadByte(uint8_t *data, uint32_t timeoutMs)
{
    uint32_t startTick; // 保存本次等待开始时间。
    if (data == NULL || __get_PRIMASK() != 0U || __get_BASEPRI() != 0U || __get_IPSR() != 0U) return BSP_RS485_INVALID;
    if (!s_initialized) return BSP_RS485_NOT_INITIALIZED;
    startTick = BSP_Time_GetMs();
    for (;;) {
        if (s_rx_error) return BSP_RS485_RX_ERROR;
        if (BSP_ByteRing_Pop(&s_rx, data) != BSP_BOARD_LOW) return BSP_RS485_OK;
        if ((uint32_t)(BSP_Time_GetMs() - startTick) >= timeoutMs) return BSP_RS485_TIMEOUT;
        __WFI();
    }
}
/** @brief 读取目标BSP独立毫秒时基。
 * @return 自然回绕的32位毫秒计数。 */
uint32_t BSP_Rs485_GetTickMs(void) { return BSP_Time_GetMs(); }
/** @brief 按SR/DR顺序接收字节，记录奇偶、帧、噪声、溢出及队列满错误。 */
void BSP_Rs485_IrqHandler(void)
{
    uint32_t status = USART3->SR; // 保存本次接收硬件状态。
    if ((status & (USART_SR_RXNE | USART_SR_PE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U) {
        uint8_t byte = (uint8_t)USART3->DR; // 保存字节并清除相应硬件状态。
        if ((status & (USART_SR_PE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U) s_rx_error = true;
        else if ((status & USART_SR_RXNE) != 0U && BSP_ByteRing_Push(&s_rx, byte) == BSP_BOARD_LOW) s_rx_error = true;
    }
}
/** @brief 衔接本板唯一USART3接收向量。 */
void USART3_IRQHandler(void) { BSP_Rs485_IrqHandler(); }
