#ifndef BSP_BOARD_H
#define BSP_BOARD_H

/* 板卡：STM32F103C8T6；当前系统时钟由 HSI直接提供，保持原生成工程配置。 */
#define BSP_BOARD_MCU_NAME              "STM32F103C8T6"
#define BSP_BOARD_MAIN_CLOCK_HZ         8000000UL
#define BSP_BOARD_SYSCLK_HZ             BSP_BOARD_MAIN_CLOCK_HZ
#define BSP_BOARD_HCLK_HZ               BSP_BOARD_MAIN_CLOCK_HZ
#define BSP_BOARD_PCLK1_HZ              BSP_BOARD_HCLK_HZ
#define BSP_BOARD_PCLK2_HZ              BSP_BOARD_HCLK_HZ
/* 当前APB1/APB2均不分频，定时器时钟等于对应PCLK。 */
#define BSP_BOARD_APB1_TIMER_HZ         BSP_BOARD_PCLK1_HZ

#define BSP_BOARD_APB2_TIMER_HZ BSP_BOARD_PCLK2_HZ

#include <stdint.h>

/* 保持既有HSI/8MHz时钟树；基础毫秒时钟和RTOS Tick均为1kHz。 */
#define BSP_BOARD_TICK_HZ             1000UL
#define BSP_BOARD_CLOCK_WAIT_LIMIT    100000UL
#define BSP_BOARD_PRIORITY_GROUP      3U
#define BSP_BOARD_HSI_TRIM_DEFAULT    16UL
#define BSP_BOARD_SYSTICK_OK          0UL

/* RM0008：SWD所属GPIO字段及启动时钟字段；符号由官方CMSIS头文件提供。 */
#define BOARD_DEBUG_CLOCKS (RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN)
#define BOARD_DEBUG_FIELDS (GPIO_CRH_MODE13 | GPIO_CRH_CNF13 | GPIO_CRH_MODE14 | GPIO_CRH_CNF14)
#define BOARD_DEBUG_INPUT_PULL (GPIO_CRH_CNF13_1 | GPIO_CRH_CNF14_1)
#define BOARD_DEBUG_PULL_MASK (GPIO_ODR_ODR13 | GPIO_ODR_ODR14)
#define BOARD_DEBUG_PULL_VALUE GPIO_ODR_ODR13
#define BOARD_CLOCK_FIELDS (RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)
#define BOARD_CLOCK_HSI_UNDIVIDED RCC_CFGR_SW_HSI
#define BOARD_FLASH_ZERO_WAIT 0U

/** @brief 板级启动结果，不代表外接SWD或按键实物已验证。 */
typedef enum {
    BSP_BOARD_OK = 0, // 可读配置与时钟核对成功。
    BSP_BOARD_DEBUG_ERROR = -1, // SWD所属可读GPIO配置或时钟使能核对失败。
    BSP_BOARD_CLOCK_ERROR = -2, // HSI就绪、切换或分频核对失败。
    BSP_BOARD_TIME_ERROR = -3 // 基础SysTick配置失败。
} BspBoardResult;

/**
 * @brief 显式保留SWD关闭JTAG，建立8MHz时钟和兼容既有HAL驱动的毫秒时基。
 * @return 成功返回BSP_BOARD_OK，其余返回对应启动错误。
 */
BspBoardResult BSP_Board_Init(void);

#endif /* BSP_BOARD_H */

