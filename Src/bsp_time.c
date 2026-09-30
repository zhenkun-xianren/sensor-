#include "bsp_time.h"
#include "bsp_board.h"
#include "stm32f103xb.h"

#define BSP_TIME_SYSTICK_RELOAD_MAX 0x00FFFFFFUL
#define BSP_TIME_SYSTICK_PRIORITY   15UL

static volatile uint32_t s_tick_ms; // 自板级时基启动以来累计的毫秒数。

/**
 * @brief 将 SysTick 配为每毫秒一次的板级时基。
 * @return 装载值有效并配置完成返回 1，否则返回 0。
 */
uint8_t BSP_Time_Init(void)
{
    uint32_t ticks_per_ms = BSP_BOARD_HCLK_HZ / BSP_TIME_TICK_HZ; // 一次毫秒中断需要的内核时钟周期数。

    if ((BSP_BOARD_HCLK_HZ % BSP_TIME_TICK_HZ) != 0U ||
        ticks_per_ms == 0U ||
        (ticks_per_ms - 1U) > BSP_TIME_SYSTICK_RELOAD_MAX) {
        return 0U;
    }

    SysTick->CTRL = 0U;
    SysTick->LOAD = ticks_per_ms - 1U;
    SysTick->VAL = 0U;
    s_tick_ms = 0U;
    NVIC_SetPriority(SysTick_IRQn, BSP_TIME_SYSTICK_PRIORITY);
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
    return 1U;
}

/**
 * @brief 获取开机以来的毫秒计数，允许自然回绕。
 * @return 当前毫秒计数。
 */
uint32_t BSP_Time_GetMs(void)
{
    return s_tick_ms;
}

/**
 * @brief 在 SysTick 中断中推进板级毫秒计数。
 */
void BSP_Time_OnSysTick(void)
{
    ++s_tick_ms;
}
