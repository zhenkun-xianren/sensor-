#include "bsp_ir.h"
#include "bsp_gpio.h"
#include "port_ir_remote.h"
#include "stm32f103xb.h"

static uint16_t s_previous_time;
static uint8_t s_previous_level;
static uint8_t s_started;

/**
 * @brief 将 TIM3 配置为微秒计数器，并启用红外信号双边沿捕获。
 */
void BSP_Ir_Init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    TIM3->CR1 = 0U;
    TIM3->PSC = BSP_IR_TIMER_PRESCALER;
    TIM3->ARR = BSP_IR_TIMER_PERIOD_TICKS - 1U;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->CR1 = TIM_CR1_CEN;
    EXTI->IMR |= EXTI_IMR_MR8;
    EXTI->RTSR |= EXTI_RTSR_TR8;
    EXTI->FTSR |= EXTI_FTSR_TR8;
    EXTI->PR = EXTI_PR_PR8;
    s_previous_time = (uint16_t)TIM3->CNT;
    s_previous_level = (GPIOA->IDR & (1U << BSP_GPIO_PA_IR_INPUT_PIN)) != 0U ? 1U : 0U;
    s_started = 0U;
    NVIC_SetPriority(EXTI9_5_IRQn, BSP_IR_EXTI_IRQ_PRIORITY);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
}

/**
 * @brief 测量最近一次红外边沿间隔并交给 NEC 解码器处理。
 */
void BSP_Ir_IrqHandler(void)
{
    uint16_t now;
    uint8_t level;
    if ((EXTI->PR & EXTI_PR_PR8) == 0U) return;
    EXTI->PR = EXTI_PR_PR8;
    now = (uint16_t)TIM3->CNT;
    level = (GPIOA->IDR & (1U << BSP_GPIO_PA_IR_INPUT_PIN)) != 0U ? 1U : 0U;
    if (s_started != 0U && level != s_previous_level) {
        PortIr_OnPulseIsr(s_previous_level, (uint16_t)(now - s_previous_time));
    }
    s_previous_time = now;
    s_previous_level = level;
    s_started = 1U;
}
