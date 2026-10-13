#include "stm32g0xx.h"
#include "bsp_board.h"
#include "bsp_time.h"

static volatile uint32_t s_milliseconds; // 保存已处理SysTick中断的毫秒数，自然回绕。

/**
 * @brief 启动阶段配置HCLK驱动的1ms SysTick，保持最低中断优先级。
 * @return 主频和24位重装值有效返回0，否则关闭SysTick并返回-1。
 */
int BSP_Time_Init(void)
{
    uint32_t counts = SystemCoreClock / BSP_TIME_TICK_HZ; // 由已核对HCLK推导每毫秒计数。
    SysTick->CTRL = 0U;
    if (SystemCoreClock != BSP_BOARD_HCLK_HZ || counts == 0U ||
        (counts - 1U) > SysTick_LOAD_RELOAD_Msk) {
        return -1;
    }
    s_milliseconds = 0U;
    NVIC_SetPriority(SysTick_IRQn, BSP_BOARD_KERNEL_IRQ_PRIORITY);
    /* PENDSTCLR为写1清挂起字段，只写目标位，禁止对ICSR普通读改写。 */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    SysTick->LOAD = counts - 1U;
    SysTick->VAL = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
    return 0;
}

/**
 * @brief 原子读取Cortex-M0+上对齐的32位计数，不引入新的中断屏蔽。
 * @return 已处理SysTick的毫秒计数，按32位自然回绕。
 */
uint32_t BSP_Time_GetMs(void)
{
    return s_milliseconds;
}

/** @brief 每次SysTick中断累计1ms；关闭Tickless条件下与RTOS保持同一节拍。 */
void BSP_Time_Tick(void)
{
    ++s_milliseconds;
}
