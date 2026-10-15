#include <stdbool.h>
#include "stm32g0xx.h"
#include "bsp_ultrasonic_tx.h"

#if (BSP_BOARD_TIM1_HZ % BSP_ULTRASONIC_TX_FREQUENCY_HZ != 0UL) || \
    (BSP_ULTRASONIC_TX_PERIOD_COUNTS % BSP_ULTRASONIC_TX_EVEN_DIVISOR != 0UL) || \
    (BSP_ULTRASONIC_TX_RELOAD > BSP_ULTRASONIC_TX_COUNTER_MAX)
#error "TIM1 clock cannot produce the configured exact 50-percent waveform"
#endif

static volatile bool s_gpio_owned = false; // 本模块已取得所列GPIO，可执行无等待的故障关闭。
static volatile bool s_timer_owned = false; // TIM1时钟已取得，不触碰其他所有者的定时器。
static volatile bool s_initialized = false; // GPIO与定时器配置已经读回核对。
static volatile bool s_starting = false; // 唤醒等待尚未结束，停止请求可取消本次启动。
static volatile bool s_running = false; // 软件已成功请求运行，不表示负载电压反馈。
static volatile bool s_continuous = false; // 当前运行波形是否为连续模式。
static volatile uint16_t s_burst_cycles; // 当前脉冲串长度，重复请求不得重置计数。
static volatile uint32_t s_start_generation; // 停止时递增，使被取消的旧启动不能重新开启输出。
volatile BspUltrasonicTxDebug g_bsp_ultrasonic_tx_debug = {0}; // 保留结束IRQ寄存器和参考时间，供用户复测短脉冲。

static DevStatus bsp_ultrasonic_tx_start(bool continuous, uint16_t cycleCount);

/**
 * @brief 按正式引脚表配置发射GPIO、接收高钳位、UCPD下拉解除及独占TIM1。
 * @return 首次或重复初始化成功DEV_OK，配置或资源冲突DEV_IO_ERROR。
 */
DevStatus BSP_UltrasonicTx_Init(void)
{
    uint32_t irq_state; // 保存进入短临界区前的中断屏蔽状态。
    bool configured; // 保存本次GPIO与TIM1配置读回结果。
    if (s_initialized) return DEV_OK;
    if (BSP_Board_CheckClock() != BSP_ULTRASONIC_TX_RESET_FIELDS) return DEV_IO_ERROR;
    irq_state = __get_PRIMASK();
    __disable_irq();
    /* 不抢占UCPD1、未由本模块取得的TIM1或PC4/PC5残留USART1。 */
    if ((RCC->APBENR1 & RCC_APBENR1_UCPD1EN) != BSP_ULTRASONIC_TX_RESET_FIELDS ||
        ((RCC->APBENR2 & RCC_APBENR2_TIM1EN) != BSP_ULTRASONIC_TX_RESET_FIELDS && !s_timer_owned) ||
        (RCC->APBENR2 & RCC_APBENR2_USART1EN) != BSP_ULTRASONIC_TX_RESET_FIELDS) {
        __set_PRIMASK(irq_state);
        return DEV_IO_ERROR;
    }
    RCC->IOPENR |= BSP_ULTRASONIC_TX_GPIO_CLOCKS;
    (void)RCC->IOPENR;
    if ((RCC->IOPENR & BSP_ULTRASONIC_TX_GPIO_CLOCKS) != BSP_ULTRASONIC_TX_GPIO_CLOCKS) {
        __set_PRIMASK(irq_state);
        return DEV_IO_ERROR;
    }
    s_gpio_owned = true;
    /* BSRR先预置关闭电平，再切换输出模式；只更新本模块所属引脚字段。 */
    GPIOA->BSRR = BSP_ULTRASONIC_TX_GATE_A_OFF | GPIO_BSRR_BR8 | BSP_ULTRASONIC_TX_RX_CLAMP_A_ON;
    GPIOB->BSRR = BSP_ULTRASONIC_TX_GATE_B_OFF | BSP_ULTRASONIC_TX_SLEEP_OFF | GPIO_BSRR_BR13 | BSP_ULTRASONIC_TX_RX_CLAMP_B_ON;
    GPIOC->BSRR = BSP_ULTRASONIC_TX_GATE_C_OFF | BSP_ULTRASONIC_TX_RX_CLAMP_C_ON;
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    (void)RCC->APBENR2;
    /* RM0444第248页：UCPD1关闭时，此脉冲解除PA8/PB15的默认CC下拉。 */
    SYSCFG->CFGR1 |= SYSCFG_CFGR1_UCPD1_STROBE;
    GPIOA->OTYPER &= ~BSP_ULTRASONIC_TX_A_OUTPUT_MASK;
    GPIOB->OTYPER &= ~BSP_ULTRASONIC_TX_B_OUTPUT_MASK;
    GPIOC->OTYPER &= ~BSP_ULTRASONIC_TX_GATE_C_MASK;
    GPIOA->PUPDR = (GPIOA->PUPDR & ~BSP_ULTRASONIC_TX_A_PULL_MASK) | BSP_ULTRASONIC_TX_A_PULL_BITS;
    GPIOB->PUPDR = (GPIOB->PUPDR & ~BSP_ULTRASONIC_TX_B_PULL_MASK) | BSP_ULTRASONIC_TX_B_PULL_BITS;
    GPIOC->PUPDR = (GPIOC->PUPDR & ~GPIO_PUPDR_PUPD4) | GPIO_PUPDR_PUPD4_1;
    GPIOA->OSPEEDR = (GPIOA->OSPEEDR & ~BSP_ULTRASONIC_TX_A_SPEED_MASK) | GPIO_OSPEEDR_OSPEED8_1;
    GPIOB->OSPEEDR = (GPIOB->OSPEEDR & ~BSP_ULTRASONIC_TX_B_SPEED_MASK) | GPIO_OSPEEDR_OSPEED13_1;
    GPIOC->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED4;
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~BSP_ULTRASONIC_TX_A_AF_MASK) | BSP_ULTRASONIC_TX_A_AF_BITS;
    GPIOB->AFR[1] = (GPIOB->AFR[1] & ~BSP_ULTRASONIC_TX_B_AF_MASK) | BSP_ULTRASONIC_TX_B_AF_BITS;
    GPIOA->MODER = (GPIOA->MODER & ~BSP_ULTRASONIC_TX_A_MODE_MASK) | BSP_ULTRASONIC_TX_A_MODE_BITS;
    GPIOB->MODER = (GPIOB->MODER & ~BSP_ULTRASONIC_TX_B_MODE_MASK) | BSP_ULTRASONIC_TX_B_MODE_BITS;
    GPIOC->MODER = (GPIOC->MODER & ~BSP_ULTRASONIC_TX_C_MODE_MASK) | BSP_ULTRASONIC_TX_C_MODE_BITS;
    /* 预置高钳位后配置接收保护GPIO；PA3保持复位模式，接收模拟资源不启动。 */
    GPIOA->OTYPER &= ~BSP_ULTRASONIC_TX_RX_CLAMP_A_MASK;
    GPIOB->OTYPER &= ~BSP_ULTRASONIC_TX_RX_CLAMP_B_MASK;
    GPIOC->OTYPER &= ~BSP_ULTRASONIC_TX_RX_CLAMP_C_MASK;
    GPIOA->PUPDR = (GPIOA->PUPDR & ~BSP_ULTRASONIC_TX_RX_A_PULL_MASK) | BSP_ULTRASONIC_TX_RX_A_PULL_BITS;
    GPIOB->PUPDR = (GPIOB->PUPDR & ~BSP_ULTRASONIC_TX_RX_B_PULL_MASK) | BSP_ULTRASONIC_TX_RX_B_PULL_BITS;
    GPIOC->PUPDR = (GPIOC->PUPDR & ~BSP_ULTRASONIC_TX_RX_C_PULL_MASK) | BSP_ULTRASONIC_TX_RX_C_PULL_BITS;
    GPIOA->OSPEEDR &= ~BSP_ULTRASONIC_TX_RX_A_SPEED_MASK;
    GPIOB->OSPEEDR &= ~BSP_ULTRASONIC_TX_RX_B_SPEED_MASK;
    GPIOC->OSPEEDR &= ~BSP_ULTRASONIC_TX_RX_C_SPEED_MASK;
    GPIOA->MODER = (GPIOA->MODER & ~BSP_ULTRASONIC_TX_RX_A_MODE_MASK) | BSP_ULTRASONIC_TX_RX_A_MODE_BITS;
    GPIOB->MODER = (GPIOB->MODER & ~BSP_ULTRASONIC_TX_RX_B_MODE_MASK) | BSP_ULTRASONIC_TX_RX_B_MODE_BITS;
    GPIOC->MODER = (GPIOC->MODER & ~BSP_ULTRASONIC_TX_RX_C_MODE_MASK) | BSP_ULTRASONIC_TX_RX_C_MODE_BITS;
    RCC->APBENR2 |= RCC_APBENR2_TIM1EN;
    (void)RCC->APBENR2;
    if ((RCC->APBENR2 & RCC_APBENR2_TIM1EN) == BSP_ULTRASONIC_TX_RESET_FIELDS) {
        (void)BSP_UltrasonicTx_Stop();
        __set_PRIMASK(irq_state);
        return DEV_IO_ERROR;
    }
    s_timer_owned = true;
    RCC->APBRSTR2 |= RCC_APBRSTR2_TIM1RST;
    RCC->APBRSTR2 &= ~RCC_APBRSTR2_TIM1RST;
    TIM1->CR1 = TIM_CR1_ARPE; /* 向上计数、边沿对齐；启动前保持CEN关闭。 */
    TIM1->CR2 = TIM_CR2_MMS_0; /* TRGO=CEN供测距同步；OIS1/OIS1N均低。 */
    TIM1->DIER = BSP_ULTRASONIC_TX_RESET_FIELDS; /* 不启用任何逐周期中断或DMA。 */
    TIM1->PSC = BSP_ULTRASONIC_TX_PRESCALER;
    TIM1->ARR = BSP_ULTRASONIC_TX_RELOAD;
    TIM1->RCR = BSP_ULTRASONIC_TX_RESET_FIELDS;
    TIM1->CCR1 = BSP_ULTRASONIC_TX_HALF_COUNTS;
    TIM1->CCMR1 = BSP_ULTRASONIC_TX_TIMER_MODE; /* PWM1=110，CC1S=00，比较值预装载。 */
    TIM1->CCER = BSP_ULTRASONIC_TX_TIMER_OUTPUTS; /* CC1P/CC1NP均清零，两路有效高而互补。 */
    TIM1->BDTR = BSP_ULTRASONIC_TX_TIMER_IDLE; /* MOE=0、DTG=0、LOCK=0；驱动器自行插入内部死区。 */
    TIM1->EGR = TIM_EGR_UG; /* 把PSC/ARR/CCR预装载写入有效计数配置。 */
    TIM1->SR = BSP_ULTRASONIC_TX_RESET_FIELDS; /* 专用定时器初始化时清除UG产生的状态。 */
    TIM1->CNT = BSP_ULTRASONIC_TX_RESET_FIELDS;
    NVIC_DisableIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
    NVIC_ClearPendingIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
    NVIC_SetPriority(TIM1_BRK_UP_TRG_COM_IRQn, BSP_ULTRASONIC_TX_END_IRQ_PRIORITY);
    __DSB();
    configured =
        (GPIOA->MODER & BSP_ULTRASONIC_TX_A_MODE_MASK) == BSP_ULTRASONIC_TX_A_MODE_BITS &&
        (GPIOB->MODER & BSP_ULTRASONIC_TX_B_MODE_MASK) == BSP_ULTRASONIC_TX_B_MODE_BITS &&
        (GPIOC->MODER & BSP_ULTRASONIC_TX_C_MODE_MASK) == BSP_ULTRASONIC_TX_C_MODE_BITS &&
        (GPIOA->AFR[1] & BSP_ULTRASONIC_TX_A_AF_MASK) == BSP_ULTRASONIC_TX_A_AF_BITS &&
        (GPIOB->AFR[1] & BSP_ULTRASONIC_TX_B_AF_MASK) == BSP_ULTRASONIC_TX_B_AF_BITS &&
        (GPIOA->ODR & BSP_ULTRASONIC_TX_GATE_A_MASK) == BSP_ULTRASONIC_TX_RESET_FIELDS &&
        (GPIOB->ODR & (BSP_ULTRASONIC_TX_GATE_B_MASK | BSP_ULTRASONIC_TX_SLEEP_MASK)) == BSP_ULTRASONIC_TX_RESET_FIELDS &&
        (GPIOC->ODR & BSP_ULTRASONIC_TX_GATE_C_MASK) == BSP_ULTRASONIC_TX_RESET_FIELDS &&
        (GPIOA->MODER & BSP_ULTRASONIC_TX_RX_A_MODE_MASK) == BSP_ULTRASONIC_TX_RX_A_MODE_BITS &&
        (GPIOB->MODER & BSP_ULTRASONIC_TX_RX_B_MODE_MASK) == BSP_ULTRASONIC_TX_RX_B_MODE_BITS &&
        (GPIOC->MODER & BSP_ULTRASONIC_TX_RX_C_MODE_MASK) == BSP_ULTRASONIC_TX_RX_C_MODE_BITS &&
        (GPIOA->ODR & BSP_ULTRASONIC_TX_RX_CLAMP_A_MASK) == BSP_ULTRASONIC_TX_RX_CLAMP_A_MASK &&
        (GPIOB->ODR & BSP_ULTRASONIC_TX_RX_CLAMP_B_MASK) == BSP_ULTRASONIC_TX_RX_CLAMP_B_MASK &&
        (GPIOC->ODR & BSP_ULTRASONIC_TX_RX_CLAMP_C_MASK) == BSP_ULTRASONIC_TX_RX_CLAMP_C_MASK &&
        TIM1->CR1 == TIM_CR1_ARPE && TIM1->PSC == BSP_ULTRASONIC_TX_PRESCALER &&
        TIM1->ARR == BSP_ULTRASONIC_TX_RELOAD && TIM1->CCR1 == BSP_ULTRASONIC_TX_HALF_COUNTS &&
        TIM1->CCMR1 == BSP_ULTRASONIC_TX_TIMER_MODE && TIM1->CCER == BSP_ULTRASONIC_TX_TIMER_OUTPUTS &&
        TIM1->BDTR == BSP_ULTRASONIC_TX_TIMER_IDLE;
    s_initialized = configured;
    if (!configured) (void)BSP_UltrasonicTx_Stop();
    __set_PRIMASK(irq_state);
    return configured ? DEV_OK : DEV_IO_ERROR;
}

/**
 * @brief 请求连续模式；保持既有唤醒与重复启动契约。
 * @return 连续启动成功DEV_OK，冲突DEV_PENDING，其他结果由共用启动实现返回。
 */
DevStatus BSP_UltrasonicTx_StartContinuous(void)
{
    return bsp_ultrasonic_tx_start(true, BSP_ULTRASONIC_TX_CONTINUOUS_CYCLES);
}

/**
 * @brief 请求有限周期模式；结束时只有一次最终更新中断。
 * @param cycleCount 脉冲串周期数，范围1至256。
 * @return 成功DEV_OK，参数错误DEV_ARGUMENT_ERROR，其他结果由共用启动实现返回。
 */
DevStatus BSP_UltrasonicTx_StartBurst(uint16_t cycleCount)
{
    if (cycleCount < BSP_ULTRASONIC_TX_BURST_MIN_CYCLES ||
        cycleCount > BSP_ULTRASONIC_TX_BURST_MAX_CYCLES) return DEV_ARGUMENT_ERROR;
    return bsp_ultrasonic_tx_start(false, cycleCount);
}

/**
 * @brief 使用既有SysTick有限等待唤醒，复用连续与脉冲串的取消和关闭逻辑。
 * @param continuous 真为连续输出，假为有限周期输出。
 * @param cycleCount 有限周期模式的周期数，连续模式忽略。
 * @return 成功DEV_OK，启动并发DEV_PENDING，未就绪或取消DEV_NOT_READY，
 *         唤醒超时DEV_TIMEOUT，上下文或配置失败DEV_IO_ERROR。
 */
static DevStatus bsp_ultrasonic_tx_start(bool continuous, uint16_t cycleCount)
{
    uint32_t irq_state; // 保存本次短临界区之前的中断屏蔽状态。
    uint32_t started_ms; // 保存nSLEEP置高后的毫秒时基快照，差值自然回绕。
    uint32_t start_generation; // 标识本次唤醒请求，检测等待期间的停止及重新启动。
    uint32_t wake_ticks = BSP_ULTRASONIC_TX_WAKE_TICKS; // 中断屏蔽时还需观察的SysTick到零事件数。
    uint32_t polls = BSP_ULTRASONIC_TX_POLL_LIMIT; // 时基停止时的有限轮询预算，不冒称精确时间。
    bool poll_systick; // 调用者屏蔽中断时直接观察硬件标志，不依赖中断累计计时。
    bool enabled; // 保存启动后的控制寄存器及门控读回结果。
    if (!s_initialized) return DEV_NOT_READY;
    /* 异常上下文不得唤醒忙等；线程允许沿用FreeRTOS启动前的中断屏蔽状态。 */
    if (__get_IPSR() != BSP_ULTRASONIC_TX_THREAD_MODE) return DEV_IO_ERROR;
    /* 脉冲串结束依赖IRQ关闭门控，拒绝在中断屏蔽的线程中启动它。 */
    if (!continuous && __get_PRIMASK() != BSP_ULTRASONIC_TX_INTERRUPTS_ENABLED) return DEV_IO_ERROR;
    if (BSP_Board_CheckClock() != BSP_ULTRASONIC_TX_RESET_FIELDS) {
        (void)BSP_UltrasonicTx_Stop();
        return DEV_IO_ERROR;
    }
    irq_state = __get_PRIMASK();
    poll_systick = irq_state != BSP_ULTRASONIC_TX_INTERRUPTS_ENABLED;
    __disable_irq();
    if ((RCC->IOPENR & BSP_ULTRASONIC_TX_GPIO_CLOCKS) != BSP_ULTRASONIC_TX_GPIO_CLOCKS ||
        (RCC->APBENR2 & RCC_APBENR2_TIM1EN) == BSP_ULTRASONIC_TX_RESET_FIELDS ||
        (RCC->APBENR1 & RCC_APBENR1_UCPD1EN) != BSP_ULTRASONIC_TX_RESET_FIELDS ||
        TIM1->PSC != BSP_ULTRASONIC_TX_PRESCALER || TIM1->ARR != BSP_ULTRASONIC_TX_RELOAD ||
        TIM1->CCR1 != BSP_ULTRASONIC_TX_HALF_COUNTS || TIM1->CCMR1 != BSP_ULTRASONIC_TX_TIMER_MODE ||
        TIM1->CCER != BSP_ULTRASONIC_TX_TIMER_OUTPUTS || TIM1->CR2 != TIM_CR2_MMS_0 ||
        (SysTick->CTRL & BSP_ULTRASONIC_TX_SYSTICK_CONFIG) != BSP_ULTRASONIC_TX_SYSTICK_CONFIG ||
        SysTick->LOAD != BSP_ULTRASONIC_TX_SYSTICK_RELOAD) {
        (void)BSP_UltrasonicTx_Stop();
        __set_PRIMASK(irq_state);
        return DEV_IO_ERROR;
    }
    if (s_running) {
        if (continuous != s_continuous || (!continuous && cycleCount != s_burst_cycles)) {
            __set_PRIMASK(irq_state);
            return DEV_PENDING;
        }
        enabled = (TIM1->CR1 & TIM_CR1_CEN) != BSP_ULTRASONIC_TX_RESET_FIELDS &&
                  (TIM1->BDTR & TIM_BDTR_MOE) != BSP_ULTRASONIC_TX_RESET_FIELDS &&
                  (GPIOA->ODR & BSP_ULTRASONIC_TX_GATE_A_MASK) == BSP_ULTRASONIC_TX_ACTIVE_GATE_A_MASK &&
                  (GPIOB->ODR & (BSP_ULTRASONIC_TX_GATE_B_MASK | BSP_ULTRASONIC_TX_SLEEP_MASK)) == BSP_ULTRASONIC_TX_SLEEP_MASK &&
                  (GPIOC->ODR & BSP_ULTRASONIC_TX_GATE_C_MASK) == BSP_ULTRASONIC_TX_ACTIVE_GATE_C_MASK;
        if (!enabled) (void)BSP_UltrasonicTx_Stop();
        __set_PRIMASK(irq_state);
        return enabled ? DEV_OK : DEV_IO_ERROR;
    }
    if (s_starting) {
        __set_PRIMASK(irq_state);
        return DEV_PENDING;
    }
    GPIOA->BSRR = BSP_ULTRASONIC_TX_GATE_A_OFF;
    GPIOB->BSRR = BSP_ULTRASONIC_TX_GATE_B_OFF;
    GPIOC->BSRR = BSP_ULTRASONIC_TX_GATE_C_OFF | BSP_ULTRASONIC_TX_RX_CLAMP_C_ON;
    GPIOB->BSRR = BSP_ULTRASONIC_TX_SLEEP_ON;
    s_starting = true;
    start_generation = s_start_generation;
    __DSB();
    /* PM0223第70/71页：读取CTRL只清COUNTFLAG，不重置计数或清除中断挂起。
     * 唤醒后丢弃旧事件，再观察3次到零，消除首个不足1ms的计数相位。 */
    if (poll_systick) (void)SysTick->CTRL;
    __set_PRIMASK(irq_state);
    started_ms = BSP_Time_GetMs();
    for (;;) {
        if (!s_starting || start_generation != s_start_generation) return DEV_NOT_READY;
        if (poll_systick) {
            if ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) != BSP_ULTRASONIC_TX_RESET_FIELDS) {
                --wake_ticks;
            }
            if (wake_ticks == 0UL) break;
        } else if ((uint32_t)(BSP_Time_GetMs() - started_ms) >= BSP_ULTRASONIC_TX_WAKE_TICKS) {
            break;
        }
        if (polls == 0UL) {
            /* 只关闭本次请求；等待期间若被停止并重新启动，不能取消新的请求。 */
            irq_state = __get_PRIMASK();
            __disable_irq();
            if (!s_starting || start_generation != s_start_generation) {
                __set_PRIMASK(irq_state);
                return DEV_NOT_READY;
            }
            (void)BSP_UltrasonicTx_Stop();
            __set_PRIMASK(irq_state);
            return DEV_TIMEOUT;
        }
        --polls;
        __NOP();
    }
    irq_state = __get_PRIMASK();
    __disable_irq();
    if (!s_starting || start_generation != s_start_generation) {
        __set_PRIMASK(irq_state);
        return DEV_NOT_READY;
    }
    /* UG先装入RCR；确认UIF已经生成再清除，避免清除过早留下启动更新中断。 */
    TIM1->CR1 = TIM_CR1_ARPE | (continuous ? BSP_ULTRASONIC_TX_RESET_FIELDS : TIM_CR1_OPM);
    TIM1->DIER = BSP_ULTRASONIC_TX_RESET_FIELDS;
    TIM1->RCR = continuous ? BSP_ULTRASONIC_TX_RESET_FIELDS : (uint32_t)cycleCount - 1UL;
    TIM1->CNT = BSP_ULTRASONIC_TX_RESET_FIELDS;
    TIM1->SR = BSP_ULTRASONIC_TX_RESET_FIELDS;
    TIM1->EGR = TIM_EGR_UG;
    polls = BSP_ULTRASONIC_TX_UPDATE_POLL_LIMIT;
    while ((TIM1->SR & TIM_SR_UIF) == BSP_ULTRASONIC_TX_RESET_FIELDS && polls != 0UL) {
        --polls;
        __NOP();
    }
    if ((TIM1->SR & TIM_SR_UIF) == BSP_ULTRASONIC_TX_RESET_FIELDS) {
        ++g_bsp_ultrasonic_tx_debug.update_wait_failed_count;
        (void)BSP_UltrasonicTx_Stop();
        __set_PRIMASK(irq_state);
        return DEV_IO_ERROR;
    }
    TIM1->SR = BSP_ULTRASONIC_TX_RESET_FIELDS;
    __DSB();
    if ((TIM1->SR & TIM_SR_UIF) != BSP_ULTRASONIC_TX_RESET_FIELDS ||
        TIM1->RCR != (continuous ? BSP_ULTRASONIC_TX_RESET_FIELDS : (uint32_t)cycleCount - 1UL) ||
        TIM1->CR1 != (TIM_CR1_ARPE | (continuous ? BSP_ULTRASONIC_TX_RESET_FIELDS : TIM_CR1_OPM))) {
        ++g_bsp_ultrasonic_tx_debug.update_wait_failed_count;
        (void)BSP_UltrasonicTx_Stop();
        __set_PRIMASK(irq_state);
        return DEV_IO_ERROR;
    }
    NVIC_ClearPendingIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
    if (continuous) NVIC_DisableIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
    else {
        TIM1->DIER = TIM_DIER_UIE;
        NVIC_EnableIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
    }
    TIM1->BDTR |= TIM_BDTR_MOE;
    TIM1->CR1 |= TIM_CR1_CEN;
    /* 第2路跨端口，在同一短临界区内依次开启；不存在逐周期软件翻转。 */
    GPIOA->BSRR = BSP_ULTRASONIC_TX_ACTIVE_GATE_A_ON;
    GPIOC->BSRR = BSP_ULTRASONIC_TX_ACTIVE_GATE_C_ON;
    __DSB();
    enabled = (TIM1->CR1 & TIM_CR1_CEN) != BSP_ULTRASONIC_TX_RESET_FIELDS &&
              (TIM1->BDTR & TIM_BDTR_MOE) != BSP_ULTRASONIC_TX_RESET_FIELDS &&
              (GPIOA->ODR & BSP_ULTRASONIC_TX_GATE_A_MASK) == BSP_ULTRASONIC_TX_ACTIVE_GATE_A_MASK &&
              (GPIOB->ODR & (BSP_ULTRASONIC_TX_GATE_B_MASK | BSP_ULTRASONIC_TX_SLEEP_MASK)) == BSP_ULTRASONIC_TX_SLEEP_MASK &&
              (GPIOC->ODR & BSP_ULTRASONIC_TX_GATE_C_MASK) == BSP_ULTRASONIC_TX_ACTIVE_GATE_C_MASK;
    s_running = enabled;
    s_continuous = continuous;
    s_burst_cycles = cycleCount;
    s_starting = false;
    if (!enabled) (void)BSP_UltrasonicTx_Stop();
    __set_PRIMASK(irq_state);
    return enabled ? DEV_OK : DEV_IO_ERROR;
}

/**
 * @brief 无等待关闭发射并保持四路接收高钳位，取消尚未完成的启动。
 * @return 关闭完成DEV_OK，未取得GPIO资源DEV_NOT_READY，关闭核对失败DEV_IO_ERROR。
 */
DevStatus BSP_UltrasonicTx_Stop(void)
{
    uint32_t irq_state = __get_PRIMASK(); // 即使从故障或中断上下文进入，也恢复原屏蔽状态。
    bool closed = true; // 累积控制寄存器读回结果，不作为实际高压输出反馈。
    __disable_irq();
    s_starting = false;
    s_running = false;
    ++s_start_generation;
    if (!s_gpio_owned) {
        __set_PRIMASK(irq_state);
        return DEV_NOT_READY;
    }
    if ((RCC->IOPENR & BSP_ULTRASONIC_TX_GPIO_CLOCKS) == BSP_ULTRASONIC_TX_GPIO_CLOCKS) {
        /* 先断开所有门控，防止停止计数器后固定电平形成直流驱动。 */
        GPIOA->BSRR = BSP_ULTRASONIC_TX_GATE_A_OFF | BSP_ULTRASONIC_TX_RX_CLAMP_A_ON;
        GPIOB->BSRR = BSP_ULTRASONIC_TX_GATE_B_OFF | BSP_ULTRASONIC_TX_RX_CLAMP_B_ON;
        GPIOC->BSRR = BSP_ULTRASONIC_TX_GATE_C_OFF | BSP_ULTRASONIC_TX_RX_CLAMP_C_ON;
    } else closed = false;
    if (s_timer_owned) {
        if ((RCC->APBENR2 & RCC_APBENR2_TIM1EN) != BSP_ULTRASONIC_TX_RESET_FIELDS) {
            TIM1->BDTR &= ~TIM_BDTR_MOE;
            TIM1->DIER = BSP_ULTRASONIC_TX_RESET_FIELDS;
            TIM1->CR1 &= ~TIM_CR1_CEN;
            NVIC_DisableIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
            NVIC_ClearPendingIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
            closed = closed && (TIM1->BDTR & TIM_BDTR_MOE) == BSP_ULTRASONIC_TX_RESET_FIELDS &&
                     (TIM1->CR1 & TIM_CR1_CEN) == BSP_ULTRASONIC_TX_RESET_FIELDS;
        } else closed = false;
    }
    if ((RCC->IOPENR & BSP_ULTRASONIC_TX_GPIO_CLOCKS) == BSP_ULTRASONIC_TX_GPIO_CLOCKS) {
        GPIOB->BSRR = BSP_ULTRASONIC_TX_SLEEP_OFF;
        __DSB();
        closed = closed && (GPIOA->ODR & BSP_ULTRASONIC_TX_GATE_A_MASK) == BSP_ULTRASONIC_TX_RESET_FIELDS &&
                 (GPIOB->ODR & (BSP_ULTRASONIC_TX_GATE_B_MASK | BSP_ULTRASONIC_TX_SLEEP_MASK)) == BSP_ULTRASONIC_TX_RESET_FIELDS &&
                 (GPIOC->ODR & BSP_ULTRASONIC_TX_GATE_C_MASK) == BSP_ULTRASONIC_TX_RESET_FIELDS &&
                 (GPIOA->ODR & BSP_ULTRASONIC_TX_RX_CLAMP_A_MASK) == BSP_ULTRASONIC_TX_RX_CLAMP_A_MASK &&
                 (GPIOB->ODR & BSP_ULTRASONIC_TX_RX_CLAMP_B_MASK) == BSP_ULTRASONIC_TX_RX_CLAMP_B_MASK &&
                 (GPIOC->ODR & BSP_ULTRASONIC_TX_RX_CLAMP_C_MASK) == BSP_ULTRASONIC_TX_RX_CLAMP_C_MASK;
    }
    __set_PRIMASK(irq_state);
    return closed ? DEV_OK : DEV_IO_ERROR;
}

/** @brief 最终更新后先断开门控，再记录IRQ快照并无等待关闭驱动。 */
void TIM1_BRK_UP_TRG_COM_IRQHandler(void)
{
    uint32_t irq_state = __get_PRIMASK(); // 保留中断入口状态，快照至门控关闭期间不允许被打断。
    __disable_irq();
    /* OPM只停止计数；跨端口门控全部先断开，避免第2路在诊断期间保持静态驱动。 */
    if (s_gpio_owned && (RCC->IOPENR & RCC_IOPENR_GPIOAEN) != BSP_ULTRASONIC_TX_RESET_FIELDS)
        GPIOA->BSRR = BSP_ULTRASONIC_TX_GATE_A_OFF | BSP_ULTRASONIC_TX_RX_CLAMP_A_ON;
    if (s_gpio_owned && (RCC->IOPENR & RCC_IOPENR_GPIOBEN) != BSP_ULTRASONIC_TX_RESET_FIELDS)
        GPIOB->BSRR = BSP_ULTRASONIC_TX_GATE_B_OFF;
    if (s_gpio_owned && (RCC->IOPENR & RCC_IOPENR_GPIOCEN) != BSP_ULTRASONIC_TX_RESET_FIELDS)
        GPIOC->BSRR = BSP_ULTRASONIC_TX_GATE_C_OFF;
    ++g_bsp_ultrasonic_tx_debug.irq_count;
    g_bsp_ultrasonic_tx_debug.tim3_time_valid = false;
    g_bsp_ultrasonic_tx_debug.last_irq_tim3_us = 0UL;
    if (s_timer_owned && (RCC->APBENR2 & RCC_APBENR2_TIM1EN) != BSP_ULTRASONIC_TX_RESET_FIELDS) {
        g_bsp_ultrasonic_tx_debug.last_irq_sr = TIM1->SR;
        g_bsp_ultrasonic_tx_debug.last_irq_cr1 = TIM1->CR1;
        g_bsp_ultrasonic_tx_debug.last_irq_cnt = TIM1->CNT;
        g_bsp_ultrasonic_tx_debug.last_irq_rcr = TIM1->RCR;
        g_bsp_ultrasonic_tx_debug.last_irq_cycle_count = s_burst_cycles;
    }
    /* 只观察已运行且配置匹配的TIM3，不取得资源、修改配置或影响接收计时。 */
    if ((RCC->APBENR1 & RCC_APBENR1_TIM3EN) != BSP_ULTRASONIC_TX_RESET_FIELDS &&
        TIM3->PSC == BSP_BOARD_TIM3_HZ / BSP_ULTRASONIC_TX_DEBUG_COUNTER_HZ - 1UL &&
        TIM3->ARR == BSP_ULTRASONIC_TX_COUNTER_MAX &&
        (TIM3->SMCR & (TIM_SMCR_SMS | TIM_SMCR_TS)) == (TIM_SMCR_SMS_1 | TIM_SMCR_SMS_2) &&
        (TIM3->CR1 & TIM_CR1_CEN) != BSP_ULTRASONIC_TX_RESET_FIELDS) {
        g_bsp_ultrasonic_tx_debug.last_irq_tim3_us = TIM3->CNT;
        g_bsp_ultrasonic_tx_debug.tim3_time_valid = true;
    }
    /* 本模块独占TIM1；任何意外中断源也采用受控关闭，避免保持静态驱动。 */
    (void)BSP_UltrasonicTx_Stop();
    if (s_timer_owned && (RCC->APBENR2 & RCC_APBENR2_TIM1EN) != BSP_ULTRASONIC_TX_RESET_FIELDS)
        TIM1->SR = BSP_ULTRASONIC_TX_RESET_FIELDS;
    __set_PRIMASK(irq_state);
}
