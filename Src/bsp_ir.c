#include "bsp_ir.h"
#include "stm32f103xb.h"
#include <stddef.h>

#define BSP_IR_EXTI_LINE_MASK                 EXTI_IMR_MR8
#define BSP_IR_TIMER_ENABLE_MASK              TIM_CR1_CEN
#define BSP_IR_AFIO_EXTI8_SOURCE_MASK         AFIO_EXTICR3_EXTI8
#define BSP_IR_PA8_CONFIG_SHIFT               BSP_IR_GPIO_CRH_PA8_SHIFT

static BspIrPulseCallback s_pulse_callback; // 红外脉冲测量完成后通知协议层的回调。
static uint16_t s_previous_time; // 上一次 PA8 边沿对应的 TIM3 计数值。
static uint8_t s_previous_level; // 上一次 PA8 边沿前的红外信号电平。
static uint8_t s_started; // 是否已经取得首个边沿作为计时基准。

/**
 * @brief 配置 PA8 上拉、TIM3 微秒计数和 EXTI8 双边沿捕获。
 * @param pulse_callback 收到有效输入边沿间隔时调用的协议层回调。
 * @return GPIO、定时器和中断配置及寄存器核对均成功返回 1，否则返回 0。
 */
uint8_t BSP_Ir_Init(BspIrPulseCallback pulse_callback)
{
    uint32_t timer_divider = BSP_BOARD_APB1_TIMER_HZ / BSP_IR_TIMER_COUNTER_HZ; // TIM3 输入时钟分频到 1 MHz 所需的除数。
    uint32_t gpioa_crh; // 更新 PA8 输入上拉模式时使用的 GPIOA 配置寄存器快照。

    if (pulse_callback == NULL || BSP_IR_TIMER_COUNTER_HZ == 0U ||
        (BSP_BOARD_APB1_TIMER_HZ % BSP_IR_TIMER_COUNTER_HZ) != 0U ||
        timer_divider == 0U ||
        (timer_divider - 1U) > BSP_IR_TIMER_PRESCALER_MAX) {
        return 0U;
    }

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    (void)RCC->APB2ENR;
    (void)RCC->APB1ENR;

    /* IRM-3638T 输出为开集电极；先置高 ODR，再切换 PA8 到输入上拉模式。 */
    GPIOA->BSRR = BSP_IR_GPIO_PA8_MASK;
    gpioa_crh = GPIOA->CRH;
    gpioa_crh &= ~(BSP_IR_GPIO_CRH_PA8_CONFIG_MASK << BSP_IR_GPIO_CRH_PA8_SHIFT);
    gpioa_crh |= BSP_IR_GPIO_INPUT_PULLUP_CONFIG << BSP_IR_GPIO_CRH_PA8_SHIFT;
    GPIOA->CRH = gpioa_crh;

    /* 明确将 EXTI8 映射到 PA8；PA8 是 AFIO_EXTICR3 的第一个字段。 */
    AFIO->EXTICR[2] = (AFIO->EXTICR[2] & ~BSP_IR_AFIO_EXTI8_SOURCE_MASK) |
                      AFIO_EXTICR3_EXTI8_PA;

    /* APB1 定时器时钟为 64 MHz；分频到 1 MHz 后，计数器单位为 1 微秒。 */
    TIM3->CR1 = 0U;
    TIM3->PSC = (uint16_t)(timer_divider - 1U);
    TIM3->ARR = (uint16_t)(BSP_IR_TIMER_PERIOD_TICKS - 1U);
    TIM3->CNT = 0U;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0U;
    TIM3->CR1 = BSP_IR_TIMER_ENABLE_MASK;

    EXTI->IMR &= ~BSP_IR_EXTI_LINE_MASK;
    EXTI->RTSR |= EXTI_RTSR_TR8;
    EXTI->FTSR |= EXTI_FTSR_TR8;
    EXTI->PR = BSP_IR_EXTI_LINE_MASK;

    s_previous_time = (uint16_t)TIM3->CNT;
    s_previous_level = (GPIOA->IDR & BSP_IR_GPIO_PA8_MASK) != 0U ? 1U : 0U;
    s_started = 0U;
    s_pulse_callback = pulse_callback;

    NVIC_DisableIRQ(EXTI9_5_IRQn);
    NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
    NVIC_SetPriority(EXTI9_5_IRQn, BSP_IR_EXTI_IRQ_PRIORITY);
    EXTI->IMR |= BSP_IR_EXTI_LINE_MASK;
    NVIC_EnableIRQ(EXTI9_5_IRQn);

    if ((GPIOA->ODR & BSP_IR_GPIO_PA8_MASK) == 0U ||
        ((GPIOA->CRH >> BSP_IR_GPIO_CRH_PA8_SHIFT) &
         BSP_IR_GPIO_CRH_PA8_CONFIG_MASK) != BSP_IR_GPIO_INPUT_PULLUP_CONFIG ||
        (AFIO->EXTICR[2] & BSP_IR_AFIO_EXTI8_SOURCE_MASK) != AFIO_EXTICR3_EXTI8_PA ||
        TIM3->PSC != (uint16_t)(timer_divider - 1U) ||
        TIM3->ARR != (uint16_t)(BSP_IR_TIMER_PERIOD_TICKS - 1U) ||
        (TIM3->CR1 & BSP_IR_TIMER_ENABLE_MASK) == 0U ||
        (EXTI->IMR & BSP_IR_EXTI_LINE_MASK) == 0U) {
        EXTI->IMR &= ~BSP_IR_EXTI_LINE_MASK;
        NVIC_DisableIRQ(EXTI9_5_IRQn);
        s_pulse_callback = NULL;
        return 0U;
    }
    return 1U;
}

/**
 * @brief 处理中断线 8 的电平边沿并向协议层转交脉冲时长。
 */
void BSP_Ir_IrqHandler(void)
{
    uint16_t now; // 当前 PA8 边沿对应的 TIM3 计数值。
    uint8_t level; // 当前 PA8 边沿之后读取到的红外电平。

    if ((EXTI->PR & BSP_IR_EXTI_LINE_MASK) == 0U) {
        return;
    }
    EXTI->PR = BSP_IR_EXTI_LINE_MASK;
    now = (uint16_t)TIM3->CNT;
    level = (GPIOA->IDR & BSP_IR_GPIO_PA8_MASK) != 0U ? 1U : 0U;
    if (s_started != 0U && level != s_previous_level && s_pulse_callback != NULL) {
        s_pulse_callback(s_previous_level, (uint16_t)(now - s_previous_time));
    }
    s_previous_time = now;
    s_previous_level = level;
    s_started = 1U;
}
