#include "bsp_ir.h"
#include "stm32f103xb.h"
#include <stddef.h>
#include <stdbool.h>
#if (BSP_BOARD_TIM2_HZ % BSP_IR_TIMER_COUNTER_HZ != 0UL) || (BSP_IR_TIMER_PRESCALER > UINT16_MAX)
#error IR_TIMER_CLOCK_MUST_PRODUCE_EXACT_ONE_MHZ
#endif
static BspIrPulseCallback s_callback; // 保存Port脉冲回调。
static uint64_t s_epoch; // 记录每次16位计数回绕累加的微秒高位。
static uint64_t s_previous_time; // 保存上次边沿的完整扩展时间。
static uint8_t s_previous_level; // 保存上个边沿后的电平。
static bool s_started = false; // 是否已经取得首个边沿基准。
static bool s_owned = false; // 是否拥有TIM2与EXTI8。
/** @brief 保存PRIMASK并屏蔽中断，保护Port和板级共享状态。
 * @return 原中断屏蔽状态。 */
uint32_t BSP_Ir_EnterCritical(void)
{
    uint32_t saved = __get_PRIMASK(); // 保存原中断状态。
    __disable_irq(); __DMB(); return saved;
}
/** @brief 恢复进入前的中断状态。
 * @param saved_mask 配对进入接口返回的PRIMASK。 */
void BSP_Ir_ExitCritical(uint32_t saved_mask) { __DMB(); __set_PRIMASK(saved_mask); }
/** @brief 消费TIM2更新标志并扩展16位计数；不清除其他SR标志。 */
static void ir_take_overflow(void)
{
    if ((TIM2->SR & TIM_SR_UIF) != 0U) {
        TIM2->SR = ~TIM_SR_UIF; // 更新标志写0清除，其他位写1保留。
        s_epoch += BSP_IR_TIMER_WRAP_US;
    }
}
/** @brief 将当前计数转换为与上个边沿的真实间隔，长空闲饱和。
 * @param now 当前TIM2的16位计数值。
 * @return 32位微秒间隔，超过范围时UINT32_MAX。 */
static uint32_t ir_elapsed(uint32_t now)
{
    uint64_t elapsed = s_epoch + now - s_previous_time; // 保存跨16位和32位回绕的完整间隔。
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}
/** @brief 停止本模块资源，不改变共享EXTI5至9的其他线路。 */
static void ir_stop_owned(void)
{
    if (s_owned) {
        EXTI->IMR &= ~EXTI_IMR_MR8;
        TIM2->CR1 = 0U; TIM2->DIER = 0U;
        NVIC_DisableIRQ(TIM2_IRQn);
        RCC->APB1ENR &= ~RCC_APB1ENR_TIM2EN;
        s_callback = NULL; s_owned = false;
    }
}
/** @brief 根据本板PA8和板级TIM2输入时钟初始化16位扩展计时。
 * @param callback 接收完整微秒脉冲数据的Port回调。
 * @return 成功BSP_BOARD_OK，失败BSP_BOARD_ERROR。 */
int BSP_Ir_Init(BspIrPulseCallback callback)
{
    uint32_t saved; // 初始化前的中断状态。
    uint32_t polls = BSP_IR_TIMER_VERIFY_POLLS; // 预分频更新的有限等待次数。
    if (callback == NULL) return BSP_BOARD_ERROR;
    saved = BSP_Ir_EnterCritical();
    if (BSP_Board_CheckClock() != BSP_BOARD_OK || (!s_owned &&
        ((RCC->APB1ENR & RCC_APB1ENR_TIM2EN) != 0U || (EXTI->IMR & EXTI_IMR_MR8) != 0U))) {
        ir_stop_owned(); BSP_Ir_ExitCritical(saved); return BSP_BOARD_ERROR;
    }
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
    (void)RCC->APB2ENR;
    if ((GPIOA->LCKR & (GPIO_LCKR_LCKK | GPIO_LCKR_LCK8)) == (GPIO_LCKR_LCKK | GPIO_LCKR_LCK8)) {
        ir_stop_owned(); BSP_Ir_ExitCritical(saved); return BSP_BOARD_ERROR;
    }
    EXTI->IMR &= ~EXTI_IMR_MR8;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;
    GPIOA->BSRR = GPIO_BSRR_BS8; // IRM-3638T空闲高，选择弱上拉输入。
    GPIOA->CRH = (GPIOA->CRH & ~(GPIO_CRH_MODE8 | GPIO_CRH_CNF8)) | GPIO_CRH_CNF8_1;
    AFIO->EXTICR[BSP_IR_EXTICR_INDEX] &= ~AFIO_EXTICR3_EXTI8; // EXTI8选择GPIOA，不改其他线路。
    EXTI->RTSR |= EXTI_RTSR_TR8; EXTI->FTSR |= EXTI_FTSR_TR8;
    EXTI->PR = EXTI_PR_PR8; // 挂起位写1清除，只写所属线路。
    RCC->APB1RSTR |= RCC_APB1RSTR_TIM2RST; RCC->APB1RSTR &= ~RCC_APB1RSTR_TIM2RST;
    TIM2->PSC = BSP_IR_TIMER_PRESCALER; TIM2->ARR = BSP_IR_TIMER_MAX_COUNT;
    TIM2->EGR = TIM_EGR_UG;
    while ((TIM2->SR & TIM_SR_UIF) == 0U && polls != 0U) --polls;
    s_owned = true;
    if (polls == 0U || TIM2->PSC != BSP_IR_TIMER_PRESCALER || TIM2->ARR != BSP_IR_TIMER_MAX_COUNT ||
        (GPIOA->CRH & (GPIO_CRH_MODE8 | GPIO_CRH_CNF8)) != GPIO_CRH_CNF8_1 ||
        (AFIO->EXTICR[BSP_IR_EXTICR_INDEX] & AFIO_EXTICR3_EXTI8) != 0U) {
        ir_stop_owned(); BSP_Ir_ExitCritical(saved); return BSP_BOARD_ERROR;
    }
    TIM2->SR = 0U; TIM2->CNT = 0U;
    s_epoch = 0U; s_previous_time = 0U; s_started = false;
    s_previous_level = (GPIOA->IDR & BSP_IR_INPUT_MASK) != 0U ? BSP_BOARD_HIGH : BSP_BOARD_LOW;
    s_callback = callback;
    /* 同优先级防止两个IRQ互相抢占64位时间状态；不在这些IRQ调用RTOS API。 */
    NVIC_SetPriority(EXTI9_5_IRQn, BSP_IR_EXTI_IRQ_PRIORITY); NVIC_SetPriority(TIM2_IRQn, BSP_IR_EXTI_IRQ_PRIORITY);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    TIM2->DIER = TIM_DIER_UIE; TIM2->CR1 = TIM_CR1_CEN;
    EXTI->IMR |= EXTI_IMR_MR8;
    NVIC_EnableIRQ(TIM2_IRQn); NVIC_EnableIRQ(EXTI9_5_IRQn);
    BSP_Ir_ExitCritical(saved); return BSP_BOARD_OK;
}
/** @brief 测量EXTI8已结束脉冲，重复电平或处理期间新边沿通知Port丢失捕获。 */
void BSP_Ir_IrqHandler(void)
{
    uint32_t counter; // 保存16位硬件计数。
    uint32_t duration; // 保存饱和的32位真实微秒间隔。
    uint32_t timestamp; // 保存自然回绕的32位结束时间戳。
    uint8_t level; // 保存本次边沿后的输入电平。
    if (!s_owned || s_callback == NULL || (EXTI->PR & EXTI_PR_PR8) == 0U) return;
    EXTI->PR = EXTI_PR_PR8;
    ir_take_overflow(); counter = TIM2->CNT;
    /* 捕获时若刚好回绕，消费更新后重新取CNT，避免高位/低位错配。 */
    if ((TIM2->SR & TIM_SR_UIF) != 0U) { ir_take_overflow(); counter = TIM2->CNT; }
    level = (GPIOA->IDR & BSP_IR_INPUT_MASK) != 0U ? BSP_BOARD_HIGH : BSP_BOARD_LOW;
    duration = ir_elapsed(counter); timestamp = (uint32_t)(s_epoch + counter);
    if ((EXTI->PR & EXTI_PR_PR8) != 0U || (s_started && level == s_previous_level)) {
        s_callback(BSP_IR_LEVEL_LOST, duration, timestamp); s_started = false;
    } else {
        if (s_started) s_callback(s_previous_level, duration, timestamp);
        s_started = true;
    }
    s_previous_time = s_epoch + counter; s_previous_level = level;
}
/** @brief 衔接共享EXTI5至9向量，当前仅EXTI8启用，其他线路挂起位保留。 */
void EXTI9_5_IRQHandler(void) { BSP_Ir_IrqHandler(); }
/** @brief 每65.536ms扩展计数，并通知Port检查长空闲与重复帧过期。 */
void TIM2_IRQHandler(void)
{
    uint32_t counter; // 保存本次更新后的硬件计数。
    if (!s_owned || (TIM2->SR & TIM_SR_UIF) == 0U) return;
    ir_take_overflow(); counter = TIM2->CNT;
    if (s_callback != NULL) s_callback(BSP_IR_LEVEL_IDLE, ir_elapsed(counter), (uint32_t)(s_epoch + counter));
}
