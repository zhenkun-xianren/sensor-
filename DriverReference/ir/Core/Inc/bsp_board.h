#ifndef BSP_BOARD_H
#define BSP_BOARD_H

/* 来源：现有 SystemClock_Config 的 HSI/PLL、AHB/APB 分频；不修改 CubeMX 配置。 */
#define BSP_BOARD_HSI_HZ 16000000UL
#define BSP_BOARD_PLL_M 1UL
#define BSP_BOARD_PLL_N 8UL
#define BSP_BOARD_PLL_P 2UL
#define BSP_BOARD_PLL_Q 2UL
#define BSP_BOARD_PLL_R 2UL
#define BSP_BOARD_HSI_TRIM_DEFAULT 64UL /* RCC_ICSCR.HSITRIM默认中心值，沿用原生成配置。 */
#define BSP_BOARD_FLASH_WAIT_STATES 2UL /* RM0444 Rev6表13：Range1的64MHz需要2WS。 */
#define BSP_BOARD_KERNEL_IRQ_PRIORITY 3UL /* Cortex-M0+最低优先级，沿用原PendSV/SysTick配置。 */
#define BSP_BOARD_AHB_DIV 1UL
#define BSP_BOARD_APB_DIV 1UL
#define BSP_BOARD_SYSCLK_HZ ((BSP_BOARD_HSI_HZ / BSP_BOARD_PLL_M) * BSP_BOARD_PLL_N / BSP_BOARD_PLL_R)
#define BSP_BOARD_HCLK_HZ (BSP_BOARD_SYSCLK_HZ / BSP_BOARD_AHB_DIV)
#define BSP_BOARD_PCLK1_HZ (BSP_BOARD_HCLK_HZ / BSP_BOARD_APB_DIV)
#define BSP_BOARD_HAS_PCLK2 0U
/* STM32G071 只有一条 APB 总线，无独立 PCLK2，禁止虚构 PCLK2 数值。 */
#define BSP_BOARD_APB_TIMER_HZ (BSP_BOARD_PCLK1_HZ * ((BSP_BOARD_APB_DIV == 1UL) ? 1UL : 2UL))
#define BSP_BOARD_TIM1_HZ BSP_BOARD_APB_TIMER_HZ
#define BSP_BOARD_TIM2_HZ BSP_BOARD_APB_TIMER_HZ
#define BSP_BOARD_TIM15_HZ BSP_BOARD_APB_TIMER_HZ
#define BSP_BOARD_NVIC_PRIORITY_BITS 2U

/**
 * @brief 只读核对现有系统及总线时钟，不修改引脚、时钟或生成配置。
 * @return 0 表示当前时钟与框架定义一致，非 0 表示不一致。
 */
int BSP_Board_CheckClock(void);

#endif
