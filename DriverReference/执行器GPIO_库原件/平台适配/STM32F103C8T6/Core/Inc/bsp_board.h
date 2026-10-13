#ifndef BSP_BOARD_H
#define BSP_BOARD_H

/* 板卡：STM32F103C8T6；当前系统时钟由 HSI/2 经 PLL 倍频得到。 */
#define BSP_BOARD_MCU_NAME              "STM32F103C8T6"
#define BSP_BOARD_MAIN_CLOCK_HZ         64000000UL
#define BSP_BOARD_SYSCLK_HZ             BSP_BOARD_MAIN_CLOCK_HZ
#define BSP_BOARD_HCLK_HZ               BSP_BOARD_MAIN_CLOCK_HZ
#define BSP_BOARD_PCLK1_HZ              (BSP_BOARD_HCLK_HZ / 2UL)
#define BSP_BOARD_PCLK2_HZ              BSP_BOARD_HCLK_HZ
/* APB1 分频不为 1 时，STM32F1 的 APB1 定时器时钟为 PCLK1 的 2 倍。 */
#define BSP_BOARD_APB1_TIMER_HZ         (BSP_BOARD_PCLK1_HZ * 2UL)

#endif /* BSP_BOARD_H */
