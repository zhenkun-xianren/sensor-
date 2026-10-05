#include "bsp_ir.h"
#include "stm32f103xb.h"
#include <stddef.h>
#if (BSP_BOARD_APB1_TIMER_HZ % BSP_IR_TIMER_COUNTER_HZ) != 0U
#error "TIM3 clock must be divisible by 1 MHz"
#endif
#if BSP_IR_TIMER_PRESCALER > 65535UL
#error "TIM3 prescaler exceeds 16 bits"
#endif
static volatile uint32_t s_time_high; // TIM3 溢出累加的微秒高位。
static uint32_t s_previous_time; // 上一边沿的32位微秒时间戳。
static uint8_t s_previous_level; // 上一边沿之后的电平。
static uint8_t s_started; // 是否已有起始边沿可用于测量脉冲。
static BspIrPulseCallback s_callback; // 接收已结束脉冲的协议层回调。
volatile uint32_t g_bsp_ir_edges; // 已服务的红外边沿数量，供 SWD 观察。
/**
 * @brief 获取含待处理中断溢出的32位微秒时间戳。
 * @return 自 TIM3 启动以来的微秒数，允许自然回绕。
 */
static uint32_t ir_time_us(void)
{
    uint32_t high = s_time_high; // 已由 TIM3 中断确认的累计高位。
    uint32_t low = TIM3->CNT; // 定时器当前16位计数值。
    /* 两个 IRQ 使用相同抢占优先级，读取期间高位不会被 TIM3 ISR 修改。
       若计数器先回绕、溢出中断尚未执行，则补入高位并重新读低位。 */
    if ((TIM3->SR & TIM_SR_UIF) != 0U) {
        high += BSP_IR_TIMER_PERIOD_TICKS;
        low = TIM3->CNT;
    }
    return high + low;
}
/**
 * @brief 屏蔽中断以保护跨中断和任务共享的事件缓冲。
 * @return 原 PRIMASK 值。
 */
uint32_t BSP_Ir_Lock(void)
{
    uint32_t mask = __get_PRIMASK(); // 临界区之前的全局中断屏蔽状态。
    __disable_irq();
    __DMB();
    return mask;
}
/**
 * @brief 恢复共享缓冲访问前的中断状态。
 * @param mask 进入临界区前的 PRIMASK 值。
 */
void BSP_Ir_Unlock(uint32_t mask)
{
    __DMB();
    __set_PRIMASK(mask);
}
/**
 * @brief 释放 PA15 的 JTAG 功能，配置 EXTI15 双边沿和 TIM3 微秒时基。
 * @param callback 接收电平脉冲的非空协议层回调。
 * @return 配置成功返回1，参数无效返回0。
 */
uint8_t BSP_Ir_Init(BspIrPulseCallback callback)
{
    uint32_t mask; // 初始化期间保护共享寄存器与中断状态。
    if (callback == NULL) return 0U;
    mask = BSP_Ir_Lock();
    NVIC_DisableIRQ(EXTI15_10_IRQn);
    NVIC_DisableIRQ(TIM3_IRQn);
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    (void)RCC->APB1ENR;
    /* RM0008 9.4.2/31.4：PA15 默认 JTDI。仅重写 SWJ_CFG，保留 SPI1 重映射。
       SWJ_CFG 读回不确定，写入完整 JTAGDISABLE 字段，PA13/PA14 仍可 SWD。 */
    AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
    /* 接收头主动输出解调电平，采用浮空输入，不增加未经要求的外围电路。 */
    GPIOA->CRH = (GPIOA->CRH & ~(BSP_IR_GPIO_FIELD_MASK << BSP_IR_GPIO_CRH_SHIFT)) |
                 (BSP_IR_GPIO_INPUT_FLOATING << BSP_IR_GPIO_CRH_SHIFT);
    EXTI->IMR &= ~EXTI_IMR_MR15;
    AFIO->EXTICR[BSP_IR_EXTICR_INDEX] &= ~AFIO_EXTICR4_EXTI15;
    EXTI->RTSR |= EXTI_RTSR_TR15;
    EXTI->FTSR |= EXTI_FTSR_TR15;
    /* EXTI_PR 为写1清零，不能读改写，以免清掉其他中断线。 */
    EXTI->PR = EXTI_PR_PR15;
    TIM3->CR1 = 0U;
    TIM3->DIER = 0U;
    TIM3->SMCR = 0U;
    TIM3->CCER = 0U;
    TIM3->PSC = BSP_IR_TIMER_PRESCALER;
    TIM3->ARR = BSP_IR_TIMER_PERIOD_TICKS - 1UL;
    TIM3->CNT = 0U;
    TIM3->EGR = TIM_EGR_UG;
    /* TIM3 由本模块独占，仅启用更新中断；SR 写0清除初始化 UG 的 UIF。 */
    TIM3->SR = 0U;
    s_time_high = 0U;
    s_previous_time = 0U;
    s_previous_level = 0U;
    s_started = 0U;
    s_callback = callback;
    g_bsp_ir_edges = 0U;
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
    NVIC_SetPriority(TIM3_IRQn, BSP_IR_IRQ_PRIORITY);
    NVIC_SetPriority(EXTI15_10_IRQn, BSP_IR_IRQ_PRIORITY);
    TIM3->DIER = TIM_DIER_UIE;
    TIM3->CR1 = TIM_CR1_CEN;
    EXTI->IMR |= EXTI_IMR_MR15;
    NVIC_EnableIRQ(TIM3_IRQn);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
    BSP_Ir_Unlock(mask);
    return 1U;
}
/**
 * @brief 累加16位定时器回绕，使长时间空闲不会被误判成短脉冲。
 */
void TIM3_IRQHandler(void)
{
    if ((TIM3->SR & TIM_SR_UIF) != 0U) {
        /* 本模块独占 TIM3 且只启用 UIF，SR 直接写0避免读改写清除竞争。 */
        TIM3->SR = 0U;
        s_time_high += BSP_IR_TIMER_PERIOD_TICKS;
    }
}
/**
 * @brief 测量 PA15 边沿间的电平时长并调用协议层。
 */
void EXTI15_10_IRQHandler(void)
{
    uint32_t now; // 当前边沿的32位微秒时间戳。
    uint8_t level; // 当前边沿之后的输入电平。
    if ((EXTI->PR & EXTI_PR_PR15) == 0U) return;
    EXTI->PR = EXTI_PR_PR15;
    now = ir_time_us();
    level = (GPIOA->IDR & BSP_IR_INPUT_MASK) != 0U ? 1U : 0U;
    ++g_bsp_ir_edges;
    if (s_started != 0U && s_callback != NULL) {
        if (level != s_previous_level) {
            s_callback(s_previous_level, (uint32_t)(now - s_previous_time), now);
        } else {
            /* 边沿合并或毛刺使电平未变化时丢弃在途帧，避免拼接错误数据。 */
            s_callback(level, 0U, now);
        }
    }
    s_previous_time = now;
    s_previous_level = level;
    s_started = 1U;
}
