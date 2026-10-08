#ifndef BSP_GPIO_BOARD_H
#define BSP_GPIO_BOARD_H
#include "bsp_gpio.h"
#include "stm32f103xb.h"
/* 正式表V1.0：PB8经U97驱动低边N沟道Q13；RM0008：2MHz推挽。 */
#define BSP_GPIO_RELAY_MASK GPIO_ODR_ODR8
#define BSP_GPIO_RELAY_FIELDS (GPIO_CRH_MODE8 | GPIO_CRH_CNF8)
#define BSP_GPIO_RELAY_MODE GPIO_CRH_MODE8_1
#define BSP_GPIO_BSRR_RESET_SHIFT GPIO_BSRR_BR0_Pos
#define BSP_GPIO_CLOCKS RCC_APB2ENR_IOPBEN
enum {
    BSP_GPIO_OK = 0, // 板级操作成功。
    BSP_GPIO_ERROR = -1, // 配置或输出失败。
    BSP_GPIO_LOW = 0, // 原始低电平。
    BSP_GPIO_HIGH = 1 // 原始高电平。
};
#endif
