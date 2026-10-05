#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include <stdint.h>

/* LEDTest 实测可启动的 HSI 8 MHz / 2 × 16 时钟树。 */
#define BSP_BOARD_HSI_HZ                 8000000UL
#define BSP_BOARD_PLL_INPUT_HZ           (BSP_BOARD_HSI_HZ / 2UL)
#define BSP_BOARD_PLL_MULTIPLIER         16UL
#define BSP_BOARD_SYSCLK_HZ              (BSP_BOARD_PLL_INPUT_HZ * BSP_BOARD_PLL_MULTIPLIER)
#define BSP_BOARD_HCLK_HZ                BSP_BOARD_SYSCLK_HZ
#define BSP_BOARD_PCLK1_HZ               (BSP_BOARD_HCLK_HZ / 2UL)
#define BSP_BOARD_PCLK2_HZ               BSP_BOARD_HCLK_HZ
#define BSP_BOARD_APB1_TIMER_HZ          (BSP_BOARD_PCLK1_HZ * 2UL)
#define BSP_BOARD_APB2_TIMER_HZ          BSP_BOARD_PCLK2_HZ
#define BSP_BOARD_CLOCK_WAIT_LIMIT       1000000UL

typedef enum {
    BSP_BOARD_CLOCK_OK = 0,
    BSP_BOARD_CLOCK_HSI_TIMEOUT,
    BSP_BOARD_CLOCK_SWITCH_HSI_TIMEOUT,
    BSP_BOARD_CLOCK_PLL_STOP_TIMEOUT,
    BSP_BOARD_CLOCK_FLASH_LATENCY_ERROR,
    BSP_BOARD_CLOCK_PLL_START_TIMEOUT,
    BSP_BOARD_CLOCK_SWITCH_PLL_TIMEOUT,
    BSP_BOARD_CLOCK_FREQUENCY_ERROR
} BspBoardClockStatus;

/**
 * @brief 用寄存器配置与已验证 LEDTest 一致的 HSI 64 MHz 时钟。
 * @return 成功返回 BSP_BOARD_CLOCK_OK；失败返回对应阶段的错误状态。
 */
BspBoardClockStatus BSP_Board_ClockInit(void);

#endif /* BSP_BOARD_H */
