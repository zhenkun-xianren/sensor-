#include <stddef.h>
#include "stm32g0xx.h"
#include "bsp_ultrasonic_range.h"

#if (BSP_BOARD_TIM3_HZ % BSP_ULTRASONIC_RANGE_COUNTER_HZ != 0UL) || \
    (BSP_ULTRASONIC_RANGE_PRESCALER > BSP_ULTRASONIC_RANGE_COUNTER_MAX)
#error "TIM3 clock cannot produce the configured one-microsecond counter"
#endif
#if BSP_ULTRASONIC_RANGE_QUIET_US < BSP_ULTRASONIC_RANGE_RELEASE_GUARD_US
#error "Receive quiet interval must cover the release guard"
#endif

volatile BspUltrasonicRangeGateDebug g_bsp_ultrasonic_range_gate_debug = {0}; // 累计诊断不因每轮测量清零。

static volatile bool s_gpio_owned = false; // 只清理本模块已取得的接收GPIO。
static volatile bool s_timer_owned = false; // 只清理本模块已取得的TIM3。
static bool s_analog_owned = false; // 记录DAC1通道1与所选内部比较器均未被其他模块占用。
static volatile bool s_initialized = false; // 初始化读回成功后才允许开始测量。
static volatile bool s_active = false; // 本次测量仍在唤醒、消隐或等待回波。
static volatile bool s_listening = false; // 已经过消隐、放开接收且开启捕获。
static volatile uint32_t s_generation; // Stop取消旧启动，防止旧线程重新提交结果。
static volatile uint32_t s_candidate_first; // 当前连续周期候选的首沿时间。
static volatile uint32_t s_candidate_last; // 当前候选最近一次边沿时间。
static volatile uint32_t s_candidate_count; // 当前候选连续合格的边沿数。
static volatile bool s_previous_capture_valid = false; // 当前计数窗口内是否已有前一边沿；回绕/覆盖后重新建立周期。
static volatile bool s_waiting_for_quiet = false; // 多沿测距首次候选等待静默，单沿和只接收不应用。
static volatile uint32_t s_last_activity_us; // 实际开放或最近捕获沿时刻，包含保护期间拒绝的沿。
static uint32_t s_minimum_echo_us; // 初始化时推导距离下限对应的原始传播时间，捕获IRQ仅比较。
static DevUltrasonicRangeConfig s_config; // 初始化时复制的固定测量参数。
static volatile DevUltrasonicRangeResult s_result; // ISR写入的原始结果，只在临界区复制。

static DevStatus bsp_ultrasonic_range_validate_config(const DevUltrasonicRangeConfig *config);
static void bsp_ultrasonic_range_capture_edge(uint32_t capturedTimeUs);
static void bsp_ultrasonic_range_finish(DevUltrasonicRangeState state, uint32_t echoTimeUs);

/** @brief 检查计数覆盖、门限、发射消隐及首次回波静默间隔，拒绝无法实现的参数。
 * @param config 非空配置对象。
 * @return 合法DEV_OK，不合法DEV_ARGUMENT_ERROR。 */
static DevStatus bsp_ultrasonic_range_validate_config(const DevUltrasonicRangeConfig *config)
{
    uint32_t burst_us; // 所配置脉冲串的硬件持续时间。
    if (config == NULL) return DEV_ARGUMENT_ERROR;
    if (config->burst_cycles < BSP_ULTRASONIC_TX_BURST_MIN_CYCLES ||
        config->burst_cycles > BSP_ULTRASONIC_TX_BURST_MAX_CYCLES ||
        config->threshold_code < BSP_ULTRASONIC_RANGE_DAC_MIN_CODE ||
        config->threshold_code > BSP_ULTRASONIC_RANGE_DAC_MAX_CODE ||
        config->max_distance_mm == 0UL || config->max_distance_mm > BSP_ULTRASONIC_RANGE_DISTANCE_MAX_MM ||
        config->min_distance_mm > config->max_distance_mm ||
        config->sound_speed_mm_s < BSP_ULTRASONIC_RANGE_SOUND_MIN_MM_S ||
        config->sound_speed_mm_s > BSP_ULTRASONIC_RANGE_SOUND_MAX_MM_S ||
        config->time_offset_us < -BSP_ULTRASONIC_RANGE_OFFSET_LIMIT_US ||
        config->time_offset_us > BSP_ULTRASONIC_RANGE_OFFSET_LIMIT_US) return DEV_ARGUMENT_ERROR;
    burst_us = (uint32_t)config->burst_cycles * BSP_ULTRASONIC_RANGE_COUNTER_HZ / BSP_ULTRASONIC_TX_FREQUENCY_HZ;
    if (config->blanking_us <= burst_us ||
        config->listen_timeout_us > BSP_ULTRASONIC_RANGE_TIMEOUT_MAX_US ||
        config->blanking_us >= config->listen_timeout_us ||
        config->listen_timeout_us <= config->blanking_us +
            BSP_ULTRASONIC_RANGE_PERIOD_MAX_US * BSP_ULTRASONIC_RANGE_REQUIRED_EDGES +
            (config->receive_only ? BSP_ULTRASONIC_RANGE_RESET_FIELDS : BSP_ULTRASONIC_RANGE_QUIET_US))
        return DEV_ARGUMENT_ERROR;
    return DEV_OK;
}

/** @brief 取得独立选择的接收通道、内部门限和TIM3资源，保留其他模块字段。
 * @param config 非空、范围合法的测距参数。
 * @return 成功DEV_OK，参数错误DEV_ARGUMENT_ERROR，资源或读回失败DEV_IO_ERROR。 */
DevStatus BSP_UltrasonicRange_Init(const DevUltrasonicRangeConfig *config)
{
    uint32_t irq_state; // 保留调用者的中断屏蔽状态。
    DevStatus status; // 保存参数和发射初始化结果。
    bool configured; // 累计GPIO、模拟及定时器读回结果。
    int64_t minimum_echo_us; // 向上取整并加偏置，与Port扣除偏置的距离换算保持一致。
    status = bsp_ultrasonic_range_validate_config(config);
    if (status != DEV_OK) return status;
    minimum_echo_us = (int64_t)(((uint64_t)config->min_distance_mm * BSP_ULTRASONIC_RANGE_TIME_SCALE +
        config->sound_speed_mm_s - 1UL) / config->sound_speed_mm_s) + config->time_offset_us;
    if (!config->receive_only && minimum_echo_us >= (int64_t)config->listen_timeout_us) return DEV_ARGUMENT_ERROR;
    if (s_initialized) {
        /* 不允许重新初始化暗中改变正在测量的配置。 */
        return config->burst_cycles == s_config.burst_cycles && config->threshold_code == s_config.threshold_code &&
            config->blanking_us == s_config.blanking_us && config->listen_timeout_us == s_config.listen_timeout_us &&
            config->max_distance_mm == s_config.max_distance_mm && config->min_distance_mm == s_config.min_distance_mm &&
            config->sound_speed_mm_s == s_config.sound_speed_mm_s &&
            config->time_offset_us == s_config.time_offset_us &&
            config->receive_only == s_config.receive_only ? DEV_OK : DEV_ARGUMENT_ERROR;
    }
    status = BSP_UltrasonicTx_Init();
    if (status != DEV_OK) return status;
    status = BSP_UltrasonicTx_Stop();
    if (status != DEV_OK) return DEV_IO_ERROR;
    if (BSP_Board_CheckClock() != BSP_ULTRASONIC_RANGE_RESET_FIELDS) return DEV_IO_ERROR;
    irq_state = __get_PRIMASK();
    __disable_irq();
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    (void)RCC->APBENR2;
    /* COMP/DAC是共享资源，不复位整块SYSCFG/DAC，也不抢占已启用的通道。 */
    if (((RCC->APBENR1 & RCC_APBENR1_TIM3EN) != BSP_ULTRASONIC_RANGE_RESET_FIELDS && !s_timer_owned) ||
        (!s_analog_owned && (BSP_ULTRASONIC_RANGE_COMP->CSR & (COMP_CSR_EN | COMP_CSR_LOCK)) != BSP_ULTRASONIC_RANGE_RESET_FIELDS) ||
        (!s_analog_owned && (RCC->APBENR1 & RCC_APBENR1_DAC1EN) != BSP_ULTRASONIC_RANGE_RESET_FIELDS &&
            (DAC1->CR & BSP_ULTRASONIC_RANGE_DAC_CONTROL_MASK) != BSP_ULTRASONIC_RANGE_RESET_FIELDS)) {
        __set_PRIMASK(irq_state);
        return DEV_IO_ERROR;
    }
    RCC->IOPENR |= BSP_ULTRASONIC_RANGE_GPIO_CLOCKS;
    (void)RCC->IOPENR;
    if ((RCC->IOPENR & BSP_ULTRASONIC_RANGE_GPIO_CLOCKS) != BSP_ULTRASONIC_RANGE_GPIO_CLOCKS) {
        __set_PRIMASK(irq_state);
        return DEV_IO_ERROR;
    }
    s_gpio_owned = true;
    GPIOA->BSRR = BSP_ULTRASONIC_RANGE_A_CLAMP_ON;
    GPIOB->BSRR = BSP_ULTRASONIC_RANGE_B_CLAMP_ON;
    GPIOC->BSRR = BSP_ULTRASONIC_RANGE_C_CLAMP_ON;
    GPIOA->OTYPER &= ~BSP_ULTRASONIC_RANGE_A_CLAMP_MASK;
    GPIOB->OTYPER &= ~BSP_ULTRASONIC_RANGE_B_CLAMP_MASK;
    GPIOC->OTYPER &= ~BSP_ULTRASONIC_RANGE_C_CLAMP_MASK;
    GPIOA->PUPDR = (GPIOA->PUPDR & ~BSP_ULTRASONIC_RANGE_A_PULL_MASK) | BSP_ULTRASONIC_RANGE_A_PULL_BITS;
    GPIOB->PUPDR = (GPIOB->PUPDR & ~GPIO_PUPDR_PUPD12) | GPIO_PUPDR_PUPD12_0;
    GPIOC->PUPDR = (GPIOC->PUPDR & ~GPIO_PUPDR_PUPD3) | GPIO_PUPDR_PUPD3_0;
    GPIOA->OSPEEDR &= ~BSP_ULTRASONIC_RANGE_A_SPEED_MASK;
    GPIOB->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED12;
    GPIOC->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED3;
    GPIOA->MODER = (GPIOA->MODER & ~BSP_ULTRASONIC_RANGE_A_MODE_MASK) | BSP_ULTRASONIC_RANGE_A_MODE_BITS;
    GPIOB->MODER = (GPIOB->MODER & ~GPIO_MODER_MODE12) | GPIO_MODER_MODE12_0;
    GPIOC->MODER = (GPIOC->MODER & ~GPIO_MODER_MODE3) | GPIO_MODER_MODE3_0;
    RCC->APBENR1 |= RCC_APBENR1_DAC1EN | RCC_APBENR1_TIM3EN;
    (void)RCC->APBENR1;
    if ((RCC->APBENR1 & (RCC_APBENR1_DAC1EN | RCC_APBENR1_TIM3EN)) != (RCC_APBENR1_DAC1EN | RCC_APBENR1_TIM3EN)) {
        (void)BSP_UltrasonicRange_Stop();
        __set_PRIMASK(irq_state);
        return DEV_IO_ERROR;
    }
    s_timer_owned = true;
    s_analog_owned = true;
    DAC1->CR &= ~BSP_ULTRASONIC_RANGE_DAC_CONTROL_MASK;
    /* MODE1=011仅连接内部比较器，PA4仍作第2路接收钳位，不输出DAC电压。 */
    DAC1->MCR = (DAC1->MCR & ~DAC_MCR_MODE1) | BSP_ULTRASONIC_RANGE_DAC_INTERNAL_MODE;
    DAC1->DHR12R1 = config->threshold_code;
    DAC1->CR |= DAC_CR_EN1;
    BSP_ULTRASONIC_RANGE_COMP->CSR = BSP_ULTRASONIC_RANGE_COMP_CONFIG;
    RCC->APBRSTR1 |= RCC_APBRSTR1_TIM3RST;
    RCC->APBRSTR1 &= ~RCC_APBRSTR1_TIM3RST;
    TIM3->CR1 = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    TIM3->PSC = BSP_ULTRASONIC_RANGE_PRESCALER;
    TIM3->ARR = BSP_ULTRASONIC_RANGE_COUNTER_MAX;
    TIM3->CCMR1 = BSP_ULTRASONIC_RANGE_TIMER_INPUT;
    TIM3->CCMR2 = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    TIM3->CCER = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    TIM3->TISEL = BSP_ULTRASONIC_RANGE_TIMER_SELECTION;
    TIM3->DIER = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    TIM3->CNT = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    NVIC_DisableIRQ(TIM3_IRQn);
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_SetPriority(TIM3_IRQn, BSP_ULTRASONIC_RANGE_IRQ_PRIORITY);
    __DSB();
    configured = (GPIOA->MODER & BSP_ULTRASONIC_RANGE_A_MODE_MASK) == BSP_ULTRASONIC_RANGE_A_MODE_BITS &&
        (GPIOB->MODER & GPIO_MODER_MODE12) == GPIO_MODER_MODE12_0 &&
        (GPIOC->MODER & GPIO_MODER_MODE3) == GPIO_MODER_MODE3_0 &&
        (GPIOA->ODR & BSP_ULTRASONIC_RANGE_A_CLAMP_MASK) == BSP_ULTRASONIC_RANGE_A_CLAMP_MASK &&
        (GPIOB->ODR & BSP_ULTRASONIC_RANGE_B_CLAMP_MASK) == BSP_ULTRASONIC_RANGE_B_CLAMP_MASK &&
        (GPIOC->ODR & BSP_ULTRASONIC_RANGE_C_CLAMP_MASK) == BSP_ULTRASONIC_RANGE_C_CLAMP_MASK &&
        (DAC1->MCR & DAC_MCR_MODE1) == BSP_ULTRASONIC_RANGE_DAC_INTERNAL_MODE &&
        (DAC1->CR & BSP_ULTRASONIC_RANGE_DAC_CONTROL_MASK) == DAC_CR_EN1 &&
        DAC1->DHR12R1 == config->threshold_code &&
        (BSP_ULTRASONIC_RANGE_COMP->CSR & ~COMP_CSR_VALUE) == BSP_ULTRASONIC_RANGE_COMP_CONFIG &&
        TIM3->PSC == BSP_ULTRASONIC_RANGE_PRESCALER && TIM3->ARR == BSP_ULTRASONIC_RANGE_COUNTER_MAX &&
        TIM3->TISEL == BSP_ULTRASONIC_RANGE_TIMER_SELECTION && TIM3->CCMR1 == BSP_ULTRASONIC_RANGE_TIMER_INPUT &&
        TIM3->CR1 == BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    g_bsp_ultrasonic_range_gate_debug.close_odr = BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->ODR;
    g_bsp_ultrasonic_range_gate_debug.close_idr = BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->IDR;
    g_bsp_ultrasonic_range_gate_debug.last_close_us = 0UL;
    if ((g_bsp_ultrasonic_range_gate_debug.close_odr & BSP_ULTRASONIC_RANGE_SELECTED_CLAMP_MASK) ==
            BSP_ULTRASONIC_RANGE_SELECTED_CLAMP_MASK) ++g_bsp_ultrasonic_range_gate_debug.close_count;
    else ++g_bsp_ultrasonic_range_gate_debug.close_readback_failed_count;
    s_initialized = configured;
    if (configured) {
        s_config = *config;
        s_minimum_echo_us = minimum_echo_us > 0 ? (uint32_t)minimum_echo_us : 0UL;
        g_bsp_ultrasonic_range_gate_debug.minimum_echo_us = s_minimum_echo_us;
        s_result.state = DEV_ULTRASONIC_RANGE_IDLE;
        s_result.valid = false;
    } else (void)BSP_UltrasonicRange_Stop();
    __set_PRIMASK(irq_state);
    return configured ? DEV_OK : DEV_IO_ERROR;
}

/** @brief 测距用TIM1同步启动；只接收直接启动TIM3并放开接收，保持发射休眠。
 * @return 成功DEV_OK，正在测量DEV_PENDING，未就绪或取消DEV_NOT_READY，其他为启动原错误。 */
DevStatus BSP_UltrasonicRange_StartMeasurement(void)
{
    uint32_t irq_state; // 临界区之前的中断状态。
    uint32_t generation; // 本次启动的取消代号。
    DevStatus status; // 发射启动返回值，停止清理不覆盖它。
    uint32_t update_polls; // 定时器更新完成的有限轮询预算，避免旧UIF变成回绕或测距失败。
    if (!s_initialized) return DEV_NOT_READY;
    if (__get_IPSR() != BSP_ULTRASONIC_RANGE_THREAD_MODE ||
        __get_PRIMASK() != BSP_ULTRASONIC_RANGE_INTERRUPTS_ENABLED) return DEV_IO_ERROR;
    if (BSP_Board_CheckClock() != BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
        (void)BSP_UltrasonicRange_Stop();
        return DEV_IO_ERROR;
    }
    irq_state = __get_PRIMASK();
    __disable_irq();
    if (s_active) {
        __set_PRIMASK(irq_state);
        return DEV_PENDING;
    }
    if ((RCC->IOPENR & BSP_ULTRASONIC_RANGE_GPIO_CLOCKS) != BSP_ULTRASONIC_RANGE_GPIO_CLOCKS ||
        (RCC->APBENR1 & (RCC_APBENR1_DAC1EN | RCC_APBENR1_TIM3EN)) != (RCC_APBENR1_DAC1EN | RCC_APBENR1_TIM3EN) ||
        (BSP_ULTRASONIC_RANGE_COMP->CSR & ~COMP_CSR_VALUE) != BSP_ULTRASONIC_RANGE_COMP_CONFIG ||
        (DAC1->CR & BSP_ULTRASONIC_RANGE_DAC_CONTROL_MASK) != DAC_CR_EN1 ||
        (DAC1->MCR & DAC_MCR_MODE1) != BSP_ULTRASONIC_RANGE_DAC_INTERNAL_MODE ||
        DAC1->DHR12R1 != s_config.threshold_code ||
        TIM3->PSC != BSP_ULTRASONIC_RANGE_PRESCALER || TIM3->TISEL != BSP_ULTRASONIC_RANGE_TIMER_SELECTION ||
        TIM3->CCMR1 != BSP_ULTRASONIC_RANGE_TIMER_INPUT) {
        (void)BSP_UltrasonicRange_Stop();
        s_result.state = DEV_ULTRASONIC_RANGE_IO_ERROR;
        __set_PRIMASK(irq_state);
        return DEV_IO_ERROR;
    }
    status = BSP_UltrasonicTx_Stop();
    if (status != DEV_OK) {
        (void)BSP_UltrasonicRange_Stop();
        s_result.state = DEV_ULTRASONIC_RANGE_IO_ERROR;
        __set_PRIMASK(irq_state);
        return status;
    }
    BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->BSRR = BSP_ULTRASONIC_RANGE_SELECTED_CLAMP_ON;
    __DSB();
    g_bsp_ultrasonic_range_gate_debug.close_odr = BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->ODR;
    g_bsp_ultrasonic_range_gate_debug.close_idr = BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->IDR;
    g_bsp_ultrasonic_range_gate_debug.last_close_us = TIM3->CNT;
    g_bsp_ultrasonic_range_gate_debug.tx_irq_count_before_start = g_bsp_ultrasonic_tx_debug.irq_count;
    if ((g_bsp_ultrasonic_range_gate_debug.close_odr & BSP_ULTRASONIC_RANGE_SELECTED_CLAMP_MASK) !=
            BSP_ULTRASONIC_RANGE_SELECTED_CLAMP_MASK ||
        (BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->MODER & BSP_ULTRASONIC_RANGE_SELECTED_MODE_MASK) !=
            BSP_ULTRASONIC_RANGE_SELECTED_MODE_OUTPUT) {
        ++g_bsp_ultrasonic_range_gate_debug.close_readback_failed_count;
        (void)BSP_UltrasonicRange_Stop();
        s_result.state = DEV_ULTRASONIC_RANGE_IO_ERROR;
        __set_PRIMASK(irq_state);
        return DEV_IO_ERROR;
    }
    ++g_bsp_ultrasonic_range_gate_debug.close_count;
    TIM3->CR1 = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    TIM3->SMCR = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    TIM3->DIER = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    TIM3->CCER = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    TIM3->ARR = BSP_ULTRASONIC_RANGE_COUNTER_MAX;
    TIM3->BSP_ULTRASONIC_RANGE_BLANKING_CCR = s_config.blanking_us;
    TIM3->CCR3 = s_config.listen_timeout_us;
    TIM3->SR = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    TIM3->EGR = TIM_EGR_UG;
    update_polls = BSP_ULTRASONIC_RANGE_UPDATE_POLL_LIMIT;
    while ((TIM3->SR & TIM_SR_UIF) == BSP_ULTRASONIC_RANGE_RESET_FIELDS && update_polls != 0UL) {
        --update_polls;
        __NOP();
    }
    if ((TIM3->SR & TIM_SR_UIF) == BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
        (void)BSP_UltrasonicRange_Stop();
        s_result.state = DEV_ULTRASONIC_RANGE_IO_ERROR;
        __set_PRIMASK(irq_state);
        return DEV_IO_ERROR;
    }
    TIM3->CNT = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    TIM3->SR = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    /* ITR0=TIM1 TRGO；Trigger模式只由上升沿开始，TIM1结束不停止TIM3。 */
    TIM3->SMCR = BSP_ULTRASONIC_RANGE_TIMER_TRIGGER;
    TIM3->DIER = BSP_ULTRASONIC_RANGE_TIMER_WAIT_IRQS;
    s_result.distance_mm = 0UL;
    s_result.echo_time_us = 0UL;
    ++s_result.sequence;
    s_result.captured_edges = 0UL;
    s_result.rejected_edges = 0UL;
    s_result.overflow_count = 0UL;
    s_result.last_period_us = 0UL;
    s_result.qualified_groups = 0UL;
    s_result.timer_wrap_count = 0UL;
    s_result.state = DEV_ULTRASONIC_RANGE_MEASURING;
    s_result.valid = false;
    s_candidate_count = 0UL;
    s_previous_capture_valid = false;
    s_waiting_for_quiet = !s_config.receive_only;
    s_last_activity_us = s_config.blanking_us;
    s_listening = false;
    s_active = true;
    generation = ++s_generation;
    if (s_config.receive_only) {
        /* 内部时钟自由运行，关闭从模式；没有发射触发、消隐或40ms截止。 */
        TIM3->SMCR = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
        TIM3->DIER = BSP_ULTRASONIC_RANGE_TIMER_RECEIVE_IRQS;
        TIM3->CCER = BSP_ULTRASONIC_RANGE_CAPTURE_CONFIG;
        s_listening = true;
        s_result.state = DEV_ULTRASONIC_RANGE_LISTENING;
        NVIC_EnableIRQ(TIM3_IRQn);
        TIM3->CR1 = TIM_CR1_CEN;
        BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->BSRR = BSP_ULTRASONIC_RANGE_SELECTED_RECEIVE_ON;
        __DSB();
        if ((TIM3->CR1 & TIM_CR1_CEN) == BSP_ULTRASONIC_RANGE_RESET_FIELDS ||
            TIM3->SMCR != BSP_ULTRASONIC_RANGE_RESET_FIELDS ||
            TIM3->DIER != BSP_ULTRASONIC_RANGE_TIMER_RECEIVE_IRQS ||
            TIM3->CCER != BSP_ULTRASONIC_RANGE_CAPTURE_CONFIG ||
            (BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->ODR & BSP_ULTRASONIC_RANGE_SELECTED_CLAMP_MASK) != BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
            (void)BSP_UltrasonicRange_Stop();
            s_result.state = DEV_ULTRASONIC_RANGE_IO_ERROR;
            __set_PRIMASK(irq_state);
            return DEV_IO_ERROR;
        }
        __set_PRIMASK(irq_state);
        return DEV_OK; /* 发射已由前面的Stop关闭，本分支不会唤醒或启动TIM1。 */
    }
    NVIC_EnableIRQ(TIM3_IRQn);
    __DSB();
    __set_PRIMASK(irq_state);
    /* 内部DAC/COMP已配置，发射唤醒期间也为模拟电路提供稳定时间。 */
    status = BSP_UltrasonicTx_StartBurst(s_config.burst_cycles);
    irq_state = __get_PRIMASK();
    __disable_irq();
    if (generation != s_generation) {
        __set_PRIMASK(irq_state);
        return DEV_NOT_READY;
    }
    if (status != DEV_OK) {
        bsp_ultrasonic_range_finish(DEV_ULTRASONIC_RANGE_IO_ERROR, 0UL);
    } else if (s_active && (TIM3->CR1 & TIM_CR1_CEN) == BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
        /* 同步路由未启动TIM3时，不能等待40ms后冒称正常无回波。 */
        bsp_ultrasonic_range_finish(DEV_ULTRASONIC_RANGE_IO_ERROR, 0UL);
        status = DEV_IO_ERROR;
    }
    __set_PRIMASK(irq_state);
    return status;
}

/** @brief 短临界区内复制原始结果，避免ISR更新造成撕裂。
 * @param result 非空结果对象。
 * @return 完成DEV_OK，测量中DEV_PENDING，参数错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY。 */
DevStatus BSP_UltrasonicRange_ReadResult(DevUltrasonicRangeResult *result)
{
    uint32_t irq_state; // 复制期间保留调用者中断状态。
    DevStatus status; // 与完整结果同一临界区取得的完成状态。
    if (result == NULL) return DEV_ARGUMENT_ERROR;
    if (!s_initialized) return DEV_NOT_READY;
    irq_state = __get_PRIMASK();
    __disable_irq();
    *result = s_result;
    status = s_active ? DEV_PENDING : DEV_OK;
    __set_PRIMASK(irq_state);
    return status;
}

/** @brief 先关发射，再取消静默等待、捕获和同步触发，恢复接收钳位。
 * @return 成功DEV_OK，未取得接收资源DEV_NOT_READY，关闭失败DEV_IO_ERROR。 */
DevStatus BSP_UltrasonicRange_Stop(void)
{
    uint32_t irq_state = __get_PRIMASK(); // 故障上下文保留已经关闭的中断状态。
    DevStatus tx_status; // 发射关闭状态参与最终关闭结果。
    bool closed = true; // 软件读回关闭结果，不作为实物反馈。
    __disable_irq();
    tx_status = BSP_UltrasonicTx_Stop();
    ++s_generation;
    s_active = false;
    s_listening = false;
    s_waiting_for_quiet = false;
    s_previous_capture_valid = false;
    if (s_timer_owned) {
        NVIC_DisableIRQ(TIM3_IRQn);
        NVIC_ClearPendingIRQ(TIM3_IRQn);
        if ((RCC->APBENR1 & RCC_APBENR1_TIM3EN) != BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
            TIM3->DIER = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
            TIM3->CCER = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
            TIM3->SMCR = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
            TIM3->CR1 &= ~TIM_CR1_CEN;
            TIM3->SR = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
            closed = (TIM3->CR1 & TIM_CR1_CEN) == BSP_ULTRASONIC_RANGE_RESET_FIELDS;
        } else closed = false;
    }
    if (s_gpio_owned && (RCC->IOPENR & BSP_ULTRASONIC_RANGE_GPIO_CLOCKS) == BSP_ULTRASONIC_RANGE_GPIO_CLOCKS) {
        GPIOA->BSRR = BSP_ULTRASONIC_RANGE_A_CLAMP_ON;
        GPIOB->BSRR = BSP_ULTRASONIC_RANGE_B_CLAMP_ON;
        GPIOC->BSRR = BSP_ULTRASONIC_RANGE_C_CLAMP_ON;
        __DSB();
        closed = closed && (GPIOA->ODR & BSP_ULTRASONIC_RANGE_A_CLAMP_MASK) == BSP_ULTRASONIC_RANGE_A_CLAMP_MASK &&
            (GPIOB->ODR & BSP_ULTRASONIC_RANGE_B_CLAMP_MASK) == BSP_ULTRASONIC_RANGE_B_CLAMP_MASK &&
            (GPIOC->ODR & BSP_ULTRASONIC_RANGE_C_CLAMP_MASK) == BSP_ULTRASONIC_RANGE_C_CLAMP_MASK;
    } else if (s_gpio_owned) closed = false;
    closed = closed && (tx_status == DEV_OK || tx_status == DEV_NOT_READY);
    s_result.distance_mm = 0UL;
    s_result.echo_time_us = 0UL;
    s_result.last_period_us = 0UL;
    s_result.valid = false;
    s_result.state = closed ? DEV_ULTRASONIC_RANGE_STOPPED : DEV_ULTRASONIC_RANGE_IO_ERROR;
    __set_PRIMASK(irq_state);
    if (!s_gpio_owned) return DEV_NOT_READY;
    return closed ? DEV_OK : DEV_IO_ERROR;
}

/** @brief 结束测量并撤销静默等待、恢复钳位，在已保护的线程或ISR调用。
 * @param state 本次完成状态。
 * @param echoTimeUs 合格首沿原始时间，无有效回波时为零。 */
static void bsp_ultrasonic_range_finish(DevUltrasonicRangeState state, uint32_t echoTimeUs)
{
    DevStatus status; // 关闭发射失败时不发布有效回波。
    if (!s_active) return;
    TIM3->DIER = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    TIM3->CCER = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    TIM3->SMCR = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->SR = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
    BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->BSRR = BSP_ULTRASONIC_RANGE_SELECTED_CLAMP_ON;
    status = BSP_UltrasonicTx_Stop();
    __DSB();
    g_bsp_ultrasonic_range_gate_debug.close_odr = BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->ODR;
    g_bsp_ultrasonic_range_gate_debug.close_idr = BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->IDR;
    g_bsp_ultrasonic_range_gate_debug.last_close_us = TIM3->CNT;
    if ((g_bsp_ultrasonic_range_gate_debug.close_odr & BSP_ULTRASONIC_RANGE_SELECTED_CLAMP_MASK) !=
            BSP_ULTRASONIC_RANGE_SELECTED_CLAMP_MASK ||
        (BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->MODER & BSP_ULTRASONIC_RANGE_SELECTED_MODE_MASK) !=
            BSP_ULTRASONIC_RANGE_SELECTED_MODE_OUTPUT) {
        ++g_bsp_ultrasonic_range_gate_debug.close_readback_failed_count;
        status = DEV_IO_ERROR;
    } else ++g_bsp_ultrasonic_range_gate_debug.close_count;
    s_result.echo_time_us = echoTimeUs;
    s_result.state = status == DEV_OK ? state : DEV_ULTRASONIC_RANGE_IO_ERROR;
    s_result.valid = false; /* Port完成校正和范围检查后才提供有效距离。 */
    s_listening = false;
    s_waiting_for_quiet = false;
    __DMB();
    s_active = false;
}

/** @brief 按配置检查下降沿；当前连续三沿结合静默及周期资格确认，传播计时保留首沿。
 * @param capturedTimeUs 本次所选CCR捕获计数，单位us；只接收每次回绕后重建周期。 */
static void bsp_ultrasonic_range_capture_edge(uint32_t capturedTimeUs)
{
    uint32_t period; // 相邻候选下降沿间隔；单沿首沿即合格，间隔记录为零。
#if BSP_ULTRASONIC_RANGE_REQUIRED_EDGES > BSP_ULTRASONIC_RANGE_FIRST_EDGE_COUNT
    uint32_t quiet_us; // 多沿模式距实际开放或最近捕获沿间隔，用于首次回波静默确认。
#endif
    if (!s_active || !s_listening) return;
    if (!s_config.receive_only && (capturedTimeUs < s_config.blanking_us ||
        capturedTimeUs >= s_config.listen_timeout_us)) return;
    ++s_result.captured_edges;
    if (!s_config.receive_only) {
#if BSP_ULTRASONIC_RANGE_REQUIRED_EDGES > BSP_ULTRASONIC_RANGE_FIRST_EDGE_COUNT
        quiet_us = capturedTimeUs >= s_last_activity_us ? capturedTimeUs - s_last_activity_us : 0UL;
        if (capturedTimeUs >= s_last_activity_us) s_last_activity_us = capturedTimeUs;
        if (capturedTimeUs - s_config.blanking_us < BSP_ULTRASONIC_RANGE_RELEASE_GUARD_US ||
            (s_waiting_for_quiet && quiet_us < BSP_ULTRASONIC_RANGE_QUIET_US)) {
#else
        /* s_last_activity_us在单沿模式保持实际开门时间，不被保护内的沿推迟。 */
        if (capturedTimeUs < s_last_activity_us ||
            capturedTimeUs - s_last_activity_us < BSP_ULTRASONIC_RANGE_RELEASE_GUARD_US) {
#endif
            /* 多沿模式仍拒绝连续残振；单沿模式仅屏蔽实际开门后的开关瞬态。 */
            ++s_result.rejected_edges;
            s_candidate_count = 0UL;
            s_previous_capture_valid = false;
            s_result.last_period_us = 0UL;
            return;
        }
        s_waiting_for_quiet = false; /* 单沿首沿即可，或继续执行多沿模式的周期资格。 */
        if (capturedTimeUs < s_minimum_echo_us) {
            /* 已开门观察信号，但近端不参与候选，也不结束本轮，继续等待更远的回波。 */
            ++s_result.rejected_edges;
            s_candidate_count = 0UL;
            s_previous_capture_valid = false;
            s_result.last_period_us = 0UL;
            return;
        }
    }
    /* 同次IRQ的CCR可能属于回绕前，下一沿计数更小时再重建，避免无符号减法产生假长周期。 */
    if (s_config.receive_only && s_previous_capture_valid && capturedTimeUs < s_candidate_last) {
        s_result.rejected_edges += s_candidate_count;
        s_candidate_count = 0UL;
        s_previous_capture_valid = false;
    }
    period = s_previous_capture_valid ? capturedTimeUs - s_candidate_last : 0UL;
    s_result.last_period_us = period;
    if (s_candidate_count == 0UL) {
        s_candidate_first = capturedTimeUs;
        s_candidate_count = BSP_ULTRASONIC_RANGE_FIRST_EDGE_COUNT;
    } else {
        if (period >= BSP_ULTRASONIC_RANGE_PERIOD_MIN_US && period <= BSP_ULTRASONIC_RANGE_PERIOD_MAX_US)
            ++s_candidate_count;
        else {
            s_result.rejected_edges += s_candidate_count;
            s_candidate_first = capturedTimeUs;
            s_candidate_count = BSP_ULTRASONIC_RANGE_FIRST_EDGE_COUNT;
        }
    }
    s_candidate_last = capturedTimeUs;
    s_previous_capture_valid = true;
    if (s_candidate_count >= BSP_ULTRASONIC_RANGE_REQUIRED_EDGES) {
        ++s_result.qualified_groups;
        if (s_config.receive_only) s_candidate_count = 0UL;
        else bsp_ultrasonic_range_finish(DEV_ULTRASONIC_RANGE_VALID, s_candidate_first);
    }
}

/** @brief 建立实际接收开放时刻并处理窗口；只接收持续处理捕获、回绕和覆盖。 */
void TIM3_IRQHandler(void)
{
    uint32_t flags; // 本次处理前的硬件状态快照。
    uint32_t captured_us; // 通过所选CCR获得边沿时间，读取同时清除对应捕获标志。
    if (!s_timer_owned || (RCC->APBENR1 & RCC_APBENR1_TIM3EN) == BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
        NVIC_DisableIRQ(TIM3_IRQn);
        return;
    }
    flags = TIM3->SR;
    if (!s_active) {
        TIM3->SR = BSP_ULTRASONIC_RANGE_RESET_FIELDS;
        return;
    }
    if (s_config.receive_only) {
        /* SR为rc_w0；只清本次已记录的回绕/覆盖位，保留新到来的捕获标志。 */
        TIM3->SR = BSP_ULTRASONIC_RANGE_STATUS_MASK & ~(flags & (TIM_SR_UIF | BSP_ULTRASONIC_RANGE_CAPTURE_OVERFLOW_FLAG));
        if ((flags & TIM_SR_UIF) != BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
            ++s_result.timer_wrap_count;
            s_result.rejected_edges += s_candidate_count;
            s_candidate_count = 0UL;
            s_previous_capture_valid = false;
            s_result.last_period_us = 0UL;
        }
        if ((flags & BSP_ULTRASONIC_RANGE_CAPTURE_OVERFLOW_FLAG) != BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
            ++s_result.overflow_count;
            s_result.rejected_edges += s_candidate_count;
            s_candidate_count = 0UL;
            s_previous_capture_valid = false;
            s_result.last_period_us = 0UL;
        }
        if ((flags & BSP_ULTRASONIC_RANGE_CAPTURE_FLAG) != BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
            captured_us = TIM3->BSP_ULTRASONIC_RANGE_CAPTURE_CCR;
            if ((TIM3->SR & BSP_ULTRASONIC_RANGE_CAPTURE_OVERFLOW_FLAG) != BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
                ++s_result.overflow_count;
                s_result.rejected_edges += s_candidate_count;
                s_candidate_count = 0UL;
                s_previous_capture_valid = false;
                s_result.last_period_us = 0UL;
                TIM3->SR = BSP_ULTRASONIC_RANGE_STATUS_MASK & ~BSP_ULTRASONIC_RANGE_CAPTURE_OVERFLOW_FLAG;
            }
            bsp_ultrasonic_range_capture_edge(captured_us);
        }
        return;
    }
    if ((flags & (TIM_SR_UIF | BSP_ULTRASONIC_RANGE_CAPTURE_OVERFLOW_FLAG)) != BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
        ++s_result.overflow_count;
        bsp_ultrasonic_range_finish(DEV_ULTRASONIC_RANGE_CAPTURE_OVERFLOW, 0UL);
        return;
    }
    /* 截止已经到达时不再因迟到的消隐IRQ打开接收。 */
    if ((flags & BSP_ULTRASONIC_RANGE_BLANKING_FLAG) != BSP_ULTRASONIC_RANGE_RESET_FIELDS && !s_listening &&
        TIM3->CNT < s_config.listen_timeout_us) {
        TIM3->DIER &= ~BSP_ULTRASONIC_RANGE_BLANKING_IRQ;
        TIM3->SR = BSP_ULTRASONIC_RANGE_STATUS_MASK & ~(BSP_ULTRASONIC_RANGE_BLANKING_FLAG | BSP_ULTRASONIC_RANGE_CAPTURE_FLAG | BSP_ULTRASONIC_RANGE_CAPTURE_OVERFLOW_FLAG);
        /* TX结束IRQ优先级更高且会重新钳位；必须先确认它已完成硬件关闭。
         * 读回异常仅证明控制状态异常，不把此前零捕获直接归因为该路径。 */
        if ((TIM1->CR1 & TIM_CR1_CEN) != BSP_ULTRASONIC_RANGE_RESET_FIELDS ||
            (TIM1->BDTR & TIM_BDTR_MOE) != BSP_ULTRASONIC_RANGE_RESET_FIELDS ||
            (GPIOA->ODR & BSP_ULTRASONIC_TX_GATE_A_MASK) != BSP_ULTRASONIC_RANGE_RESET_FIELDS ||
            (GPIOB->ODR & (BSP_ULTRASONIC_TX_GATE_B_MASK | BSP_ULTRASONIC_TX_SLEEP_MASK)) != BSP_ULTRASONIC_RANGE_RESET_FIELDS ||
            (GPIOC->ODR & BSP_ULTRASONIC_TX_GATE_C_MASK) != BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
            ++g_bsp_ultrasonic_range_gate_debug.tx_not_stopped_count;
            bsp_ultrasonic_range_finish(DEV_ULTRASONIC_RANGE_IO_ERROR, 0UL);
            return;
        }
        TIM3->CCER = BSP_ULTRASONIC_RANGE_CAPTURE_CONFIG; /* 先准备捕获，再释放模拟钳位。 */
        TIM3->DIER |= BSP_ULTRASONIC_RANGE_CAPTURE_IRQ;
        BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->BSRR = BSP_ULTRASONIC_RANGE_SELECTED_RECEIVE_ON;
        __DSB();
        s_last_activity_us = TIM3->CNT; /* GPIO写入之后记录静默起点，适用于可调消隐值。 */
        g_bsp_ultrasonic_range_gate_debug.last_open_us = s_last_activity_us;
        g_bsp_ultrasonic_range_gate_debug.open_odr = BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->ODR;
        g_bsp_ultrasonic_range_gate_debug.open_idr = BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->IDR;
        g_bsp_ultrasonic_range_gate_debug.tx_irq_count_at_open = g_bsp_ultrasonic_tx_debug.irq_count;
        g_bsp_ultrasonic_range_gate_debug.tx_end_us_at_open = g_bsp_ultrasonic_tx_debug.last_irq_tim3_us;
        g_bsp_ultrasonic_range_gate_debug.tx_end_time_valid_at_open = g_bsp_ultrasonic_tx_debug.tim3_time_valid;
        if ((g_bsp_ultrasonic_range_gate_debug.open_odr & BSP_ULTRASONIC_RANGE_SELECTED_CLAMP_MASK) != BSP_ULTRASONIC_RANGE_RESET_FIELDS ||
            (BSP_ULTRASONIC_RANGE_RECEIVE_GPIO->MODER & BSP_ULTRASONIC_RANGE_SELECTED_MODE_MASK) != BSP_ULTRASONIC_RANGE_SELECTED_MODE_OUTPUT ||
            TIM3->CCER != BSP_ULTRASONIC_RANGE_CAPTURE_CONFIG ||
            (TIM3->DIER & BSP_ULTRASONIC_RANGE_CAPTURE_IRQ) == BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
            ++g_bsp_ultrasonic_range_gate_debug.open_readback_failed_count;
            bsp_ultrasonic_range_finish(DEV_ULTRASONIC_RANGE_IO_ERROR, 0UL);
            return;
        }
        ++g_bsp_ultrasonic_range_gate_debug.open_count;
        s_waiting_for_quiet = true;
        s_listening = true;
    }
    if ((flags & BSP_ULTRASONIC_RANGE_CAPTURE_FLAG) != BSP_ULTRASONIC_RANGE_RESET_FIELDS && s_listening) {
        captured_us = TIM3->BSP_ULTRASONIC_RANGE_CAPTURE_CCR; /* 读SR后读所选CCR清捕获标志；不RMW状态寄存器。 */
        if ((TIM3->SR & BSP_ULTRASONIC_RANGE_CAPTURE_OVERFLOW_FLAG) != BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
            ++s_result.overflow_count;
            bsp_ultrasonic_range_finish(DEV_ULTRASONIC_RANGE_CAPTURE_OVERFLOW, 0UL);
            return;
        }
        bsp_ultrasonic_range_capture_edge(captured_us);
    }
    if (s_active && (flags & TIM_SR_CC3IF) != BSP_ULTRASONIC_RANGE_RESET_FIELDS) {
        s_result.rejected_edges += s_candidate_count;
        bsp_ultrasonic_range_finish(DEV_ULTRASONIC_RANGE_NO_ECHO, 0UL);
    }
}
