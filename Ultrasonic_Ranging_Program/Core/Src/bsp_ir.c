#include "bsp_ir.h"
#include "stm32g0xx.h"
#include <stddef.h>

#if (BSP_BOARD_TIM2_HZ % BSP_IR_TIMER_COUNTER_HZ != 0UL) || (BSP_IR_TIMER_PRESCALER > 65535UL)
#error IR_TIMER_CLOCK_MUST_PRODUCE_EXACT_ONE_MHZ
#endif
#if (BSP_IR_EXTI_IRQ_PRIORITY >= (1UL << BSP_BOARD_NVIC_PRIORITY_BITS))
#error IR_IRQ_PRIORITY_OUT_OF_RANGE
#endif

static BspIrPulseCallback s_callback; // Port提供的脉冲回调。
static uint32_t s_previous_time;      // 上次边沿的计数器值。
static uint8_t s_previous_level;      // 上次边沿后的输入电平。
static uint8_t s_started;             // 是否已收到用于定位的第一个边沿。
static uint8_t s_owned;               // 本模块是否已成功取得TIM2和EXTI0。
static uint8_t s_timer_wraps;         // 两个边沿间发生的计数器回绕次数，最多保存两次。

/**
 * @brief 保存并屏蔽中断，保护队列与板级共享寄存器。
 * @return 进入前的PRIMASK状态。
 */
uint32_t BSP_Ir_EnterCritical(void)
{
    uint32_t saved_mask = __get_PRIMASK(); // 保存进入前的中断屏蔽状态。
    __disable_irq();
    __DMB();
    return saved_mask;
}

/**
 * @brief 完成内存访问后恢复原来的中断屏蔽状态。
 * @param saved_mask 配对进入函数返回的PRIMASK。
 */
void BSP_Ir_ExitCritical(uint32_t saved_mask)
{
    __DMB();
    __set_PRIMASK(saved_mask);
}

/**
 * @brief 把尚未处理的TIM2回绕加入记录，并只清除更新标志。
 */
static void ir_take_overflow(void)
{
    if ((TIM2->SR & TIM_SR_UIF) != 0U) {
        /* TIMx_SR是写0清除：写1保留其他标志，不使用读改写避免并发误清。 */
        TIM2->SR = ~TIM_SR_UIF;
        if (s_timer_wraps < BSP_IR_MAX_IDLE_WRAPS) ++s_timer_wraps;
    }
}

/**
 * @brief 计算上次边沿后的时长；超过一整个32位周期时饱和。
 * @param now 当前TIM2计数。
 * @return 微秒间隔，超出32位表示范围时返回UINT32_MAX。
 */
static uint32_t ir_elapsed(uint32_t now)
{
    if (s_timer_wraps >= BSP_IR_MAX_IDLE_WRAPS ||
        (s_timer_wraps != 0U && now >= s_previous_time)) {
        return UINT32_MAX;
    }
    return (uint32_t)(now - s_previous_time);
}

/** @brief 停止本模块已取得的接收资源，不改写其他GPIO或共享EXTI1配置。 */
static void ir_stop_owned(void)
{
    if (s_owned != 0U) {
        EXTI->IMR1 &= ~EXTI_IMR1_IM0;
        TIM2->CR1 = 0U;
        TIM2->DIER = 0U;
        NVIC_DisableIRQ(TIM2_IRQn);
        RCC->APBENR1 &= ~RCC_APBENR1_TIM2EN;
        s_callback = NULL;
        s_owned = 0U;
    }
}

/**
 * @brief 初始化红外专用资源，失败时不继续启用接收。
 * @param callback 由Port提供的非空脉冲处理回调。
 * @return 0表示成功，非0表示板级核对或资源取得失败。
 */
int BSP_Ir_Init(BspIrPulseCallback callback)
{
    uint32_t mask;                    // 初始化期间的原中断屏蔽状态。
    uint32_t polls;                   // 等待预分频更新事件的有限轮询次数。
    uint32_t original_clock;          // 失败回滚时保留原TIM2时钟状态。
    uint32_t mode;                    // 当前PD0模式，用于防止覆盖其他外设。
    if (callback == NULL) return -1;
    mask = BSP_Ir_EnterCritical();
    if (BSP_Board_CheckClock() != 0) {
        ir_stop_owned();
        BSP_Ir_ExitCritical(mask);
        return -1;
    }
    original_clock = RCC->APBENR1 & RCC_APBENR1_TIM2EN;
    /* UCPD2的CC1/CC2共用PD0/PD2；不抢占已开启的UCPD2。
     * 当前无其他EXTI0/TIM2实现；资源被其他模块取得后应显式重新分配。 */
    if ((RCC->APBENR1 & RCC_APBENR1_UCPD2EN) != 0U ||
        (s_owned == 0U && (original_clock != 0U || (EXTI->IMR1 & EXTI_IMR1_IM0) != 0U))) {
        ir_stop_owned();
        BSP_Ir_ExitCritical(mask);
        return -1;
    }
    RCC->IOPENR |= RCC_IOPENR_GPIODEN;
    (void)RCC->IOPENR; /* 读回保证GPIO时钟使能后再访问寄存器。 */
    mode = GPIOD->MODER & GPIO_MODER_MODE0;
    if (((GPIOD->LCKR & (GPIO_LCKR_LCKK | GPIO_LCKR_LCK0)) ==
         (GPIO_LCKR_LCKK | GPIO_LCKR_LCK0)) ||
        (mode != 0U && mode != GPIO_MODER_MODE0)) {
        ir_stop_owned();
        BSP_Ir_ExitCritical(mask);
        return -1;
    }
    EXTI->IMR1 &= ~EXTI_IMR1_IM0;
    NVIC_DisableIRQ(TIM2_IRQn);
    s_callback = NULL;
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    RCC->APBENR1 |= RCC_APBENR1_TIM2EN;
    (void)RCC->APBENR1;
    (void)RCC->APBENR2;
    /* RM0444 Rev2第214页：UCPD2关闭时STROBE解除PD0及PD2的CC下拉。
     * 该动作只在显式初始化时执行，不能靠GPIO上拉对抗CC下拉。 */
    SYSCFG->CFGR1 |= SYSCFG_CFGR1_UCPD2_STROBE;
    GPIOD->MODER &= ~GPIO_MODER_MODE0; /* 输入模式，不改变GPIOD的其他引脚。 */
    GPIOD->PUPDR = (GPIOD->PUPDR & ~GPIO_PUPDR_PUPD0) | GPIO_PUPDR_PUPD0_0;
    EXTI->EXTICR[0] = (EXTI->EXTICR[0] & ~EXTI_EXTICR1_EXTI0) |
        (BSP_IR_EXTI_PORT_D << EXTI_EXTICR1_EXTI0_Pos);
    EXTI->RTSR1 |= EXTI_RTSR1_RT0;
    EXTI->FTSR1 |= EXTI_FTSR1_FT0;
    /* EXTI挂起寄存器写1清除，直接写本线路掩码，不能清除EXTI1。 */
    EXTI->RPR1 = EXTI_RPR1_RPIF0;
    EXTI->FPR1 = EXTI_FPR1_FPIF0;
    RCC->APBRSTR1 |= RCC_APBRSTR1_TIM2RST;
    RCC->APBRSTR1 &= ~RCC_APBRSTR1_TIM2RST;
    TIM2->CR1 = 0U;
    TIM2->PSC = BSP_IR_TIMER_PRESCALER;
    TIM2->ARR = BSP_IR_TIMER_MAX_COUNT;
    TIM2->CNT = 0U;
    TIM2->EGR = TIM_EGR_UG; /* 强制更新，把PSC缓冲值装入有效预分频器。 */
    polls = BSP_IR_TIMER_VERIFY_POLLS;
    while ((TIM2->SR & TIM_SR_UIF) == 0U && polls != 0U) --polls;
    if (polls == 0U || TIM2->PSC != BSP_IR_TIMER_PRESCALER ||
        TIM2->ARR != BSP_IR_TIMER_MAX_COUNT ||
        (GPIOD->MODER & GPIO_MODER_MODE0) != 0U ||
        (GPIOD->PUPDR & GPIO_PUPDR_PUPD0) != GPIO_PUPDR_PUPD0_0 ||
        (EXTI->EXTICR[0] & EXTI_EXTICR1_EXTI0) != BSP_IR_EXTI_PORT_D) {
        TIM2->CR1 = 0U;
        TIM2->DIER = 0U;
        RCC->APBENR1 &= ~RCC_APBENR1_TIM2EN;
        s_owned = 0U;
        BSP_Ir_ExitCritical(mask);
        return -1;
    }
    TIM2->SR = 0U; /* 专用定时器初始化阶段清除UG产生的更新标志。 */
    s_previous_time = 0U;
    s_previous_level = (GPIOD->IDR & BSP_IR_INPUT_MASK) != 0U ? 1U : 0U;
    s_started = 0U;
    s_timer_wraps = 0U;
    s_callback = callback;
    s_owned = 1U;
    NVIC_SetPriority(EXTI0_1_IRQn, BSP_IR_EXTI_IRQ_PRIORITY);
    NVIC_SetPriority(TIM2_IRQn, BSP_IR_EXTI_IRQ_PRIORITY);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    /* TIM2更新中断约每71.6分钟一次，防止跨多个回绕的空闲误认成短间隔。
     * 与EXTI相同优先级，两个处理函数不会互相抢占共享时间戳。 */
    TIM2->DIER = TIM_DIER_UIE;
    TIM2->CR1 = TIM_CR1_CEN;
    EXTI->IMR1 |= EXTI_IMR1_IM0;
    NVIC_EnableIRQ(TIM2_IRQn);
    NVIC_EnableIRQ(EXTI0_1_IRQn);
    BSP_Ir_ExitCritical(mask);
    return 0;
}

/**
 * @brief 测量本驱动的EXTI0边沿间隔，捕获异常时通知Port重新同步。
 */
void BSP_Ir_IrqHandler(void)
{
    uint32_t rising;                  // 进入中断时的本线路上升挂起标志。
    uint32_t falling;                 // 进入中断时的本线路下降挂起标志。
    uint32_t now;                     // 当前32位微秒计数。
    uint32_t duration;                // 已结束脉冲时长。
    uint8_t level;                    // 本次边沿后的输入电平。
    if (s_owned == 0U || s_callback == NULL) return;
    rising = EXTI->RPR1 & EXTI_RPR1_RPIF0;
    falling = EXTI->FPR1 & EXTI_FPR1_FPIF0;
    if ((rising | falling) == 0U) return;
    /* 先获取时间，再清除快照中的挂起位；未观察到的另一方向标志保留。 */
    ir_take_overflow();
    now = TIM2->CNT;
    if ((TIM2->SR & TIM_SR_UIF) != 0U) {
        ir_take_overflow();
        now = TIM2->CNT;
    }
    level = (GPIOD->IDR & BSP_IR_INPUT_MASK) != 0U ? 1U : 0U;
    if (rising != 0U) EXTI->RPR1 = EXTI_RPR1_RPIF0;
    if (falling != 0U) EXTI->FPR1 = EXTI_FPR1_FPIF0;
    duration = ir_elapsed(now);
    if ((rising != 0U && falling != 0U) ||
        (rising != 0U && level != 1U) || (falling != 0U && level != 0U) ||
        (s_started != 0U && level == s_previous_level)) {
        s_callback(BSP_IR_LEVEL_LOST, duration, now);
    } else if (s_started != 0U) {
        s_callback(s_previous_level, duration, now);
    }
    s_previous_time = now;
    s_previous_level = level;
    s_started = 1U;
    s_timer_wraps = 0U;
}

/** @brief 衔接启动文件的共享EXTI0/1向量；当前工程只有EXTI0使用者。 */
void EXTI0_1_IRQHandler(void)
{
    BSP_Ir_IrqHandler();
}

/** @brief 在计数器回绕时记录长空闲，并交由Port判断协议过期。 */
void TIM2_IRQHandler(void)
{
    uint32_t now;                     // 回绕中断时的计数器值。
    if (s_owned == 0U || (TIM2->SR & TIM_SR_UIF) == 0U) return;
    ir_take_overflow();
    now = TIM2->CNT;
    if (s_callback != NULL) s_callback(BSP_IR_LEVEL_IDLE, ir_elapsed(now), now);
}
