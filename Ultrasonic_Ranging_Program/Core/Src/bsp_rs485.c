/* 来源驱动库bsp_rs485.c：F103 USART2/SR/DR适配为G071 USART3/ISR/TDR/ICR。
 * 保留方向切换、逐字节TXE等待、TC等待、释放流程；移除全部接收依赖。 */
#include "bsp_rs485.h"
#include "stm32g0xx.h"
#include "bsp_time.h"
static uint8_t s_initialized; // 保存UART只发送初始化状态。

/**
 * @brief 控制同时连接DE和/RE的PA10，BSRR原子写不影响其他输出。
 * @param transmit 非零置高发送，零置低释放总线。
 */
static void rs485_set_direction(uint8_t transmit)
{
    if (transmit != 0U) {
        GPIOA->BSRR = BSP_RS485_DIR_MASK;
    } else {
        GPIOA->BSRR = BSP_RS485_DIR_MASK << BSP_RS485_BSRR_RESET_SHIFT;
    }
}
/**
 * @brief 沿用驱动库有截止时间的状态等待，使用独立BSP毫秒计时。
 * @param flag 目标USART_ISR位掩码。
 * @param startTick 本地事务起始Tick，差值允许自然回绕。
 * @return 标志置位返回1，整体50ms期限到达返回0。
 */
static uint8_t rs485_wait_status(uint32_t flag, uint32_t startTick)
{
    for (;;) {
        if ((USART3->ISR & flag) != 0U) {
            return 1U;
        }
        if ((uint32_t)(BSP_Time_GetMs() - startTick) >= BSP_RS485_TX_TIMEOUT_MS) {
            return 0U;
        }
    }
}
/** @brief 超时后释放总线并关闭UART，取消残留发送，要求下次调用先初始化。 */
static void rs485_abort(void)
{
    rs485_set_direction(0U);
    /* RM0444第1039页：UE清零停止当前操作并丢弃挂起内容。 */
    USART3->CR1 = 0U;
    s_initialized = 0U;
}
/**
 * @brief 独立寄存器初始化9600/8N1发送，不修改CubeMX。
 * @return 成功返回OK，时钟不符返回INVALID，TEACK超时返回TIMEOUT。
 */
BspRs485Status BSP_Rs485_Init(void)
{
    uint32_t primask; // 保存共享GPIO和时钟修改前的中断屏蔽状态。
    uint32_t startTick; // 保存UART发送使能确认起点。
    s_initialized = 0U;
    if (BSP_Board_CheckClock() != 0) {
        return BSP_RS485_INVALID;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIODEN;
    RCC->APBENR1 |= RCC_APBENR1_USART3EN;
    (void)RCC->IOPENR;
    (void)RCC->APBENR1;
    /* 先预置TX空闲高和DE低，再改变模式，避免初始化产生发送使能毛刺。 */
    GPIOD->BSRR = BSP_RS485_TX_MASK;
    rs485_set_direction(0U);
    GPIOA->OTYPER &= ~BSP_RS485_DIR_MASK;
    GPIOA->PUPDR &= ~(BSP_RS485_GPIO_FIELD_MASK << BSP_RS485_DIR_MODE_SHIFT);
    GPIOA->OSPEEDR &= ~(BSP_RS485_GPIO_FIELD_MASK << BSP_RS485_DIR_MODE_SHIFT);
    GPIOA->MODER = (GPIOA->MODER & ~(BSP_RS485_GPIO_FIELD_MASK << BSP_RS485_DIR_MODE_SHIFT)) |
                   (BSP_RS485_GPIO_MODE_OUTPUT << BSP_RS485_DIR_MODE_SHIFT);
    GPIOD->OTYPER &= ~BSP_RS485_TX_MASK;
    GPIOD->PUPDR &= ~(BSP_RS485_GPIO_FIELD_MASK << BSP_RS485_TX_MODE_SHIFT);
    GPIOD->OSPEEDR &= ~(BSP_RS485_GPIO_FIELD_MASK << BSP_RS485_TX_MODE_SHIFT);
    GPIOD->AFR[1] = (GPIOD->AFR[1] & ~(BSP_RS485_GPIO_AF_FIELD_MASK << BSP_RS485_TX_AF_SHIFT)) |
                    (BSP_RS485_TX_AF_VALUE << BSP_RS485_TX_AF_SHIFT);
    GPIOD->MODER = (GPIOD->MODER & ~(BSP_RS485_GPIO_FIELD_MASK << BSP_RS485_TX_MODE_SHIFT)) |
                   (BSP_RS485_GPIO_MODE_AF << BSP_RS485_TX_MODE_SHIFT);
    if (primask == 0U) {
        __enable_irq();
    }
    USART3->CR1 = 0U;
    /* M/PCE清零为8位无校验，CR2.STOP=00为1停止位；不使用DMA/硬件DE。
     * CR1仅UE/TE，RE与UART中断全部关闭；只发送版不配置PD9。 */
    USART3->CR2 = 0U;
    USART3->CR3 = 0U;
    USART3->BRR = BSP_RS485_UART_BRR_VALUE;
    USART3->ICR = USART_ICR_TCCF; /* 写1清TC，禁止对ICR读改写。 */
    USART3->CR1 = USART_CR1_UE | USART_CR1_TE;
    startTick = BSP_Time_GetMs();
    if (rs485_wait_status(USART_ISR_TEACK, startTick) == 0U) {
        rs485_abort();
        return BSP_RS485_TIMEOUT;
    }
    s_initialized = 1U;
    return BSP_RS485_OK;
}
/**
 * @brief 逐字节发送，等待最后停止位TC后释放DE；整体期限50ms。
 * @param data 待发送请求帧。
 * @param length 请求帧字节数。
 * @return 成功仅表示UART本地完成，失败返回参数、初始化或超时状态。
 */
BspRs485Status BSP_Rs485_Send(const uint8_t *data, uint8_t length)
{
    uint8_t index; // 遍历发送字节。
    uint32_t startTick; // 保存完整帧共用的期限起点。
    uint32_t guardCycles; // 控制DE使能到输出建立的最小等待。
    if (data == 0 || length == 0U) {
        return BSP_RS485_INVALID;
    }
    if (s_initialized == 0U) {
        return BSP_RS485_NOT_INITIALIZED;
    }
    startTick = BSP_Time_GetMs();
    USART3->ICR = USART_ICR_TCCF; /* 清除旧TC，TDR写入也会清TC。 */
    rs485_set_direction(1U);
    __DSB();
    for (guardCycles = 0U; guardCycles < BSP_RS485_ENABLE_GUARD_CYCLES; ++guardCycles) {
        __NOP();
    }
    for (index = 0U; index < length; ++index) {
        if (rs485_wait_status(USART_ISR_TXE_TXFNF, startTick) == 0U) {
            rs485_abort();
            return BSP_RS485_TIMEOUT;
        }
        USART3->TDR = data[index];
    }
    if (rs485_wait_status(USART_ISR_TC, startTick) == 0U) {
        rs485_abort();
        return BSP_RS485_TIMEOUT;
    }
    /* RM0444第1064页：TC包括最后停止位，TXE仅表示可继续写数据。 */
    rs485_set_direction(0U);
    return BSP_RS485_OK;
}
