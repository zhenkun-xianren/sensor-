#include <stdbool.h>
#include <stddef.h>
#include "stm32f0xx.h"
#include "bsp_rs485.h"

#define RS485_UART_ERRORS (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)
#define RS485_UART_CLEAR_ERRORS (USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NCF | USART_ICR_ORECF)
#define RS485_PIN_TYPES (GPIO_OTYPER_OT_9 | GPIO_OTYPER_OT_10 | GPIO_OTYPER_OT_11)
#define RS485_PIN_SPEEDS (GPIO_OSPEEDR_OSPEEDR9 | GPIO_OSPEEDR_OSPEEDR10 | GPIO_OSPEEDR_OSPEEDR11)
#define RS485_PIN_PULLS (GPIO_PUPDR_PUPDR9 | GPIO_PUPDR_PUPDR10 | GPIO_PUPDR_PUPDR11)
#define RS485_FIELDS_CLEAR 0UL
#define RS485_RX_MODES (GPIO_MODER_MODER9_1 | GPIO_MODER_MODER10_1 | GPIO_MODER_MODER11_0)

/** @brief 缓冲区所有权与半双工传输状态。 */
typedef enum {
    RS485_WAIT_IDLE, RS485_IDLE, RS485_RECEIVING, RS485_READY,
    RS485_HELD, RS485_TX_ENABLE, RS485_TRANSMITTING, RS485_TX_GUARD
} Rs485State;
/** @brief TIM14本次比较事件的职责。 */
typedef enum { RS485_TIMER_T15, RS485_TIMER_T35, RS485_TIMER_TX_ENABLE } Rs485TimerPhase;

static volatile Rs485State s_state = RS485_WAIT_IDLE; // 中断与主循环共享的传输状态。
static volatile Rs485TimerPhase s_timer_phase; // 保存当前比较事件的含义。
static volatile bool s_initialized = false; // 标记板级配置完成。
static volatile bool s_gap_expired = false; // 记录新字符开始前是否已超过t1.5。
static volatile bool s_line_quiet = false; // 标记是否已观察到完整t3.5静默。
static volatile bool s_collision = false; // 记录READY/HELD期间的新总线活动。
static uint8_t s_rx[DEV_MODBUS_SLAVE_ADU_CAPACITY]; // 接收缓冲区，READY/HELD期间由主循环只读拥有。
static volatile uint16_t s_rx_length; // 接收帧字节数，使用十六位以容纳256。
static const uint8_t *s_tx_data; // 指向Port持有的静态应答缓冲区。
static volatile uint16_t s_tx_length; // 异步发送总字节数。
static volatile uint16_t s_tx_index; // 下一个待写入TDR的字节位置。
static uint32_t s_tx_start_ms; // 保存本次发送开始时刻。
static uint32_t s_tx_timeout_ms; // 保存按长度计算的整帧发送期限。

/**
 * @brief 重装微秒比较事件；调用者处于中断或短临界区。
 * @param delayUs 从当前时刻起的微秒数，必须在十六位计数范围内。
 * @param phase 本次比较事件的职责。
 */
static void rs485_arm_timer(uint32_t delayUs, Rs485TimerPhase phase)
{
    TIM14->CR1 = 0U;
    TIM14->DIER = 0U;
    TIM14->CNT = 0U;
    TIM14->CCR1 = delayUs;
    TIM14->SR = 0U; /* 定时器状态位写0清除，不能使用读改写清标志。 */
    NVIC_ClearPendingIRQ(TIM14_IRQn);
    s_timer_phase = phase;
    TIM14->DIER = TIM_DIER_CC1IE;
    TIM14->CR1 = TIM_CR1_CEN;
}

/**
 * @brief 预置或释放PA11方向控制，只修改该脚。
 * @param transmitting 真表示DE高且接收器关闭，假表示总线高阻并启用接收器。
 */
static void rs485_set_direction(bool transmitting)
{
    GPIOA->BSRR = transmitting ? GPIO_BSRR_BS_11 : GPIO_BSRR_BR_11;
}

/**
 * @brief 清除本地接收寄存器和错误，不读取或修改其他外设。
 */
static void rs485_clear_rx(void)
{
    USART1->RQR = USART_RQR_RXFRQ;
    USART1->ICR = RS485_UART_CLEAR_ERRORS | USART_ICR_IDLECF;
}

/**
 * @brief 将损坏或溢出的接收帧丢弃，重新等待完整静默。
 */
static void rs485_discard_rx(void)
{
    if ((s_state == RS485_READY) || (s_state == RS485_HELD)) {
        s_collision = true; /* 不改写主循环正在使用的数据及长度。 */
    } else {
        s_rx_length = 0U;
        s_state = RS485_WAIT_IDLE;
    }
    s_line_quiet = false;
    rs485_arm_timer(BSP_RS485_T35_US, RS485_TIMER_T35);
}

/**
 * @brief 配置本功能拥有的PA9/PA10/PA11，不覆盖SWD或其他信号。
 * @return 引脚读回一致返回真，否则返回假。
 */
static bool rs485_configure_pins(void)
{
    uint32_t af = (BSP_RS485_UART_AF << GPIO_AFRH_AFSEL9_Pos) |
                  (BSP_RS485_UART_AF << GPIO_AFRH_AFSEL10_Pos); // 保存PA9/PA10的AF1配置。
    uint32_t modeMask = GPIO_MODER_MODER9 | GPIO_MODER_MODER10 | GPIO_MODER_MODER11; // 仅覆盖三个通信引脚。
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    (void)RCC->AHBENR;
    rs485_set_direction(false); /* 先建立低电平，再打开输出，避免启动时占用总线。 */
    GPIOA->OTYPER &= ~(GPIO_OTYPER_OT_9 | GPIO_OTYPER_OT_10 | GPIO_OTYPER_OT_11);
    GPIOA->OSPEEDR &= ~(GPIO_OSPEEDR_OSPEEDR9 | GPIO_OSPEEDR_OSPEEDR10 | GPIO_OSPEEDR_OSPEEDR11);
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(GPIO_PUPDR_PUPDR9 | GPIO_PUPDR_PUPDR10 | GPIO_PUPDR_PUPDR11)) |
                  GPIO_PUPDR_PUPDR10_0; /* /RE高时RO高阻，弱上拉保持UART空闲。 */
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~(GPIO_AFRH_AFSEL9 | GPIO_AFRH_AFSEL10)) | af;
    GPIOA->MODER = (GPIOA->MODER & ~modeMask) | RS485_RX_MODES;
    return ((RCC->AHBENR & RCC_AHBENR_GPIOAEN) == RCC_AHBENR_GPIOAEN) &&
           ((GPIOA->OTYPER & RS485_PIN_TYPES) == RS485_FIELDS_CLEAR) &&
           ((GPIOA->OSPEEDR & RS485_PIN_SPEEDS) == RS485_FIELDS_CLEAR) &&
           ((GPIOA->PUPDR & RS485_PIN_PULLS) == GPIO_PUPDR_PUPDR10_0) &&
           ((GPIOA->MODER & modeMask) == RS485_RX_MODES) &&
           ((GPIOA->AFR[1] & (GPIO_AFRH_AFSEL9 | GPIO_AFRH_AFSEL10)) == af) &&
           ((GPIOA->ODR & GPIO_ODR_11) == RS485_FIELDS_CLEAR);
}

/**
 * @brief 初始化裸机USART1和独占TIM14，保留当前8MHz板级时钟。
 * @return 成功返回OK，配置或就绪超时返回对应状态。
 */
BspRs485Status BSP_Rs485_Init(void)
{
    uint32_t mask = __get_PRIMASK(); // 保存调用前中断屏蔽状态。
    uint32_t remaining = BSP_BOARD_WAIT_LIMIT; // 限制UART发送器就绪等待。
    __disable_irq();
    NVIC_DisableIRQ(USART1_IRQn);
    NVIC_DisableIRQ(TIM14_IRQn);
    if (s_initialized) {
        /* 重新初始化先取消旧事务；后续配置失败也不能遗留发送占线。 */
        USART1->CR1 = 0U;
        USART1->CR3 = 0U;
        TIM14->CR1 = 0U;
        TIM14->DIER = 0U;
        rs485_set_direction(false);
        s_tx_data = NULL;
    }
    s_initialized = false;
    if ((SystemCoreClock != BSP_BOARD_HCLK_HZ) ||
        ((RCC->CFGR & (RCC_CFGR_HPRE | RCC_CFGR_PPRE)) != BSP_RS485_CLOCK_UNDIVIDED) ||
        !rs485_configure_pins()) {
        __set_PRIMASK(mask);
        return BSP_RS485_INVALID;
    }
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM14EN;
    (void)RCC->APB1ENR;
    USART1->CR1 = 0U; /* 先关闭USART，再选择该外设自己的PCLK时钟源。 */
    RCC->CFGR3 = (RCC->CFGR3 & ~RCC_CFGR3_USART1SW) | RCC_CFGR3_USART1SW_PCLK;
    USART1->CR1 = 0U;
    USART1->CR2 = 0U; /* STOP=00、无校验，CR1.M=00：库默认8N1。 */
    USART1->CR3 = USART_CR3_EIE;
    USART1->BRR = BSP_RS485_UART_BRR;
    rs485_clear_rx();
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    /* F051 USART1支持TEACK和REACK，使用有界等待确认收发器同时就绪。 */
    while ((USART1->ISR & BSP_RS485_UART_READY_MASK) != BSP_RS485_UART_READY_MASK) {
        if (remaining-- == 0U) {
            USART1->CR1 = 0U;
            rs485_set_direction(false);
            __set_PRIMASK(mask);
            return BSP_RS485_TIMEOUT;
        }
    }
    if (((RCC->APB2ENR & RCC_APB2ENR_USART1EN) == BSP_RS485_CLOCK_DISABLED) ||
        ((RCC->APB1ENR & RCC_APB1ENR_TIM14EN) == BSP_RS485_CLOCK_DISABLED) ||
        ((RCC->CFGR3 & RCC_CFGR3_USART1SW) != RCC_CFGR3_USART1SW_PCLK) ||
        (USART1->BRR != BSP_RS485_UART_BRR)) {
        USART1->CR1 = 0U;
        rs485_set_direction(false);
        __set_PRIMASK(mask);
        return BSP_RS485_INVALID;
    }
    TIM14->CR1 = 0U;
    TIM14->DIER = 0U;
    TIM14->CCMR1 = 0U; /* CC1S=输出、OC1M=冻结，仅内部比较，不引出定时器引脚。 */
    TIM14->CCER = 0U;
    TIM14->PSC = BSP_RS485_TIMER_PSC;
    TIM14->ARR = BSP_RS485_TIMER_MAX;
    TIM14->EGR = TIM_EGR_UG; /* PSC有预装载，先更新再清状态，保证首个微秒准确。 */
    TIM14->SR = 0U;
    if ((TIM14->PSC != BSP_RS485_TIMER_PSC) || (TIM14->ARR != BSP_RS485_TIMER_MAX) ||
        (TIM14->CCER != RS485_FIELDS_CLEAR) || (TIM14->CCMR1 != RS485_FIELDS_CLEAR)) {
        USART1->CR1 = 0U;
        rs485_set_direction(false);
        __set_PRIMASK(mask);
        return BSP_RS485_INVALID;
    }
    s_state = RS485_WAIT_IDLE;
    s_gap_expired = false;
    s_line_quiet = false;
    s_collision = false;
    s_rx_length = 0U;
    s_tx_index = 0U;
    s_tx_length = 0U;
    s_tx_data = NULL;
    NVIC_ClearPendingIRQ(USART1_IRQn);
    NVIC_ClearPendingIRQ(TIM14_IRQn);
    NVIC_SetPriority(USART1_IRQn, BSP_RS485_UART_IRQ_PRIORITY);
    NVIC_SetPriority(TIM14_IRQn, BSP_RS485_TIMER_IRQ_PRIORITY);
    s_initialized = true;
    rs485_arm_timer(BSP_RS485_T35_US, RS485_TIMER_T35);
    NVIC_EnableIRQ(USART1_IRQn);
    NVIC_EnableIRQ(TIM14_IRQn);
    __set_PRIMASK(mask);
    return BSP_RS485_OK;
}

/**
 * @brief 检查完整发送期限；只在超时时进入短临界区关闭发送并释放总线。
 * @return 正常、忙、未初始化或本次发送超时。
 */
BspRs485Status BSP_Rs485_Poll(void)
{
    uint32_t mask; // 保存超时故障处理前的屏蔽状态。
    if (!s_initialized) {
        return BSP_RS485_NOT_INITIALIZED;
    }
    if ((s_state == RS485_TX_ENABLE) || (s_state == RS485_TRANSMITTING)) {
        if ((uint32_t)(BSP_Board_GetMs() - s_tx_start_ms) >= s_tx_timeout_ms) {
            mask = __get_PRIMASK();
            __disable_irq();
            /* TC可能在进入临界区前刚好完成，只有仍发送中才判为超时。 */
            if ((s_state == RS485_TX_ENABLE) || (s_state == RS485_TRANSMITTING)) {
                USART1->CR1 = 0U;
                USART1->CR3 = 0U;
                TIM14->CR1 = 0U;
                TIM14->DIER = 0U;
                rs485_set_direction(false);
                s_initialized = false;
                s_tx_data = NULL;
                NVIC_DisableIRQ(USART1_IRQn);
                NVIC_DisableIRQ(TIM14_IRQn);
                __set_PRIMASK(mask);
                return BSP_RS485_TIMEOUT;
            }
            __set_PRIMASK(mask);
        }
        return BSP_RS485_BUSY;
    }
    return ((s_state == RS485_TX_GUARD) || (s_state == RS485_WAIT_IDLE)) ? BSP_RS485_BUSY : BSP_RS485_OK;
}

/**
 * @brief 将完整接收帧所有权借给主循环。
 * @param data 保存只读静态缓冲区指针。
 * @param length 保存接收字节数。
 * @return 有完整帧返回OK，否则返回NO_FRAME或参数错误。
 */
BspRs485Status BSP_Rs485_AcquireFrame(const uint8_t **data, uint16_t *length)
{
    uint32_t mask; // 保存中断屏蔽状态，原子切换所有权。
    if ((data == NULL) || (length == NULL)) {
        return BSP_RS485_INVALID;
    }
    mask = __get_PRIMASK();
    __disable_irq();
    if (s_state != RS485_READY) {
        __set_PRIMASK(mask);
        return BSP_RS485_NO_FRAME;
    }
    s_state = RS485_HELD;
    __DMB(); /* 确保所有权交接前看到中断已完成的接收数据。 */
    *data = s_rx;
    *length = s_rx_length;
    __set_PRIMASK(mask);
    return BSP_RS485_OK;
}

/** @brief 释放借用帧，出现新总线活动时只在静默完成后恢复接收。 */
void BSP_Rs485_ReleaseFrame(void)
{
    uint32_t mask = __get_PRIMASK(); // 保存所有权操作前的屏蔽状态。
    __disable_irq();
    if (s_state == RS485_HELD) {
        s_rx_length = 0U;
        s_collision = false;
        s_state = s_line_quiet ? RS485_IDLE : RS485_WAIT_IDLE;
    }
    __set_PRIMASK(mask);
}

/**
 * @brief 在IDLE主动发送或HELD应答，先等待使能建立，再由TXE中断连续发送。
 * @param data 在整个异步发送期间保持有效且不变的字节序列。
 * @param length 字节数，范围1至256。
 * @return 已启动返回OK，冲突返回BUSY，参数或初始化失败返回对应状态。
 */
BspRs485Status BSP_Rs485_StartSend(const uint8_t *data, uint16_t length)
{
    uint32_t mask; // 保存总线交接前的中断屏蔽状态。
    if ((data == NULL) || (length == 0U) || (length > DEV_MODBUS_SLAVE_ADU_CAPACITY)) {
        return BSP_RS485_INVALID;
    }
    mask = __get_PRIMASK();
    __disable_irq();
    if (!s_initialized) {
        __set_PRIMASK(mask);
        return BSP_RS485_NOT_INITIALIZED;
    }
    if (((s_state != RS485_HELD) && (s_state != RS485_IDLE)) || s_collision || !s_line_quiet ||
        ((USART1->ISR & (USART_ISR_BUSY | USART_ISR_RXNE | RS485_UART_ERRORS)) != 0U)) {
        __set_PRIMASK(mask);
        return BSP_RS485_BUSY;
    }
    s_tx_data = data;
    s_tx_length = length;
    s_tx_index = 0U;
    s_tx_start_ms = BSP_Board_GetMs();
    s_tx_timeout_ms = ((uint32_t)length * BSP_RS485_CHARACTER_BITS * BSP_RS485_MILLISECONDS +
                      BSP_RS485_UART_BAUD_RATE - 1UL) / BSP_RS485_UART_BAUD_RATE + BSP_RS485_TX_MARGIN_MS;
    s_state = RS485_TX_ENABLE;
    s_rx_length = 0U;
    s_line_quiet = false;
    USART1->CR1 &= ~(USART_CR1_RXNEIE | USART_CR1_TXEIE | USART_CR1_TCIE);
    USART1->CR3 &= ~USART_CR3_EIE;
    rs485_clear_rx();
    USART1->ICR = USART_ICR_TCCF;
    rs485_set_direction(true);
    rs485_arm_timer(BSP_RS485_TX_ENABLE_US, RS485_TIMER_TX_ENABLE);
    __set_PRIMASK(mask);
    return BSP_RS485_OK;
}

/**
 * @brief 接收一个无硬件错误的字节并维护缓冲区所有权。
 * @param value 本次接收的字节值。
 */
static void rs485_receive_byte(uint8_t value)
{
    if ((s_state == RS485_TX_ENABLE) || (s_state == RS485_TRANSMITTING)) {
        return;
    }
    s_line_quiet = false;
    if (s_state == RS485_IDLE) {
        s_rx_length = 0U;
        s_gap_expired = false;
        s_state = RS485_RECEIVING;
    }
    if (s_state == RS485_RECEIVING) {
        if (s_gap_expired || (s_rx_length >= DEV_MODBUS_SLAVE_ADU_CAPACITY)) {
            rs485_discard_rx();
            return;
        }
        s_rx[s_rx_length++] = value;
        s_gap_expired = false;
        rs485_arm_timer(BSP_RS485_T15_US, RS485_TIMER_T15);
    } else {
        if ((s_state == RS485_HELD) || (s_state == RS485_READY)) {
            s_collision = true;
        }
        rs485_arm_timer(BSP_RS485_T35_US, RS485_TIMER_T35);
    }
}

/** @brief 处理接收错误、RXNE、TXE与TC，寄存器访问集中在BSP。 */
void BSP_Rs485_IrqHandler(void)
{
    uint32_t status = USART1->ISR; // 固定本次中断入口的硬件状态。
    if ((status & RS485_UART_ERRORS) != 0U) {
        rs485_clear_rx();
        if ((s_state != RS485_TX_ENABLE) && (s_state != RS485_TRANSMITTING)) {
            rs485_discard_rx();
        }
    } else if ((status & USART_ISR_RXNE) != 0U) {
        uint8_t value = (uint8_t)USART1->RDR; // 读取RDR清除RXNE。
        rs485_receive_byte(value);
    }
    if (((USART1->CR1 & USART_CR1_TXEIE) != 0U) &&
        ((USART1->ISR & USART_ISR_TXE) != 0U) && (s_state == RS485_TRANSMITTING)) {
        USART1->TDR = s_tx_data[s_tx_index++]; /* 写TDR自动清TC，不能把TXE当作停止位已完成。 */
        if (s_tx_index == s_tx_length) {
            USART1->CR1 = (USART1->CR1 & ~USART_CR1_TXEIE) | USART_CR1_TCIE;
        }
    }
    if (((USART1->CR1 & USART_CR1_TCIE) != 0U) && ((USART1->ISR & USART_ISR_TC) != 0U)) {
        USART1->CR1 &= ~USART_CR1_TCIE;
        USART1->ICR = USART_ICR_TCCF;
        rs485_clear_rx();
        rs485_set_direction(false); /* 最后停止位已完成，现在释放DE并开启接收。 */
        USART1->CR3 |= USART_CR3_EIE;
        USART1->CR1 |= USART_CR1_RXNEIE;
        s_tx_data = NULL;
        s_state = RS485_TX_GUARD;
        s_line_quiet = false;
        rs485_arm_timer(BSP_RS485_T35_US, RS485_TIMER_T35);
    }
}

/** @brief 在短临界区内处理比较事件，防止UART重装定时器时发生交错。 */
static void rs485_handle_timer(void)
{
    if ((TIM14->SR & TIM_SR_CC1IF) == 0U) {
        return;
    }
    TIM14->SR = 0U;
    if (s_timer_phase == RS485_TIMER_TX_ENABLE) {
        TIM14->DIER = 0U;
        TIM14->CR1 = 0U;
        if (s_state == RS485_TX_ENABLE) {
            s_state = RS485_TRANSMITTING;
            USART1->CR1 |= USART_CR1_TXEIE;
        }
        return;
    }
    if (s_timer_phase == RS485_TIMER_T15) {
        /* BUSY反映已检测到起始位；下一字符正在接收时，不把合法静默误判为断帧。 */
        s_gap_expired = ((USART1->ISR & (USART_ISR_BUSY | USART_ISR_RXNE)) == 0U);
        s_timer_phase = RS485_TIMER_T35;
        TIM14->CCR1 = BSP_RS485_T35_US;
        if (TIM14->CNT < BSP_RS485_T35_US) {
            return;
        }
        /* 若中断延迟已跨过t3.5，直接完成处理，不能等待计数器再次绕回比较值。 */
    }
    if ((USART1->ISR & (USART_ISR_BUSY | USART_ISR_RXNE | RS485_UART_ERRORS)) != 0U) {
        rs485_discard_rx();
        return;
    }
    TIM14->DIER = 0U;
    TIM14->CR1 = 0U;
    s_line_quiet = true;
    if (s_state == RS485_RECEIVING) {
        __DMB(); /* 发布READY前确保缓冲区和长度已完整写入。 */
        s_state = RS485_READY;
    } else if ((s_state == RS485_WAIT_IDLE) || (s_state == RS485_TX_GUARD)) {
        s_state = RS485_IDLE;
    }
}

/** @brief 管理微秒事件；临界区有界，UART中断最多延后本次简短状态处理。 */
void BSP_Rs485_TimerIrqHandler(void)
{
    uint32_t mask = __get_PRIMASK(); // 保存进入定时器处理前的中断屏蔽状态。
    __disable_irq();
    rs485_handle_timer();
    __set_PRIMASK(mask);
}

/** @brief USART1硬件向量入口。 */
void USART1_IRQHandler(void)
{
    BSP_Rs485_IrqHandler();
}

/** @brief TIM14硬件向量入口。 */
void TIM14_IRQHandler(void)
{
    BSP_Rs485_TimerIrqHandler();
}
