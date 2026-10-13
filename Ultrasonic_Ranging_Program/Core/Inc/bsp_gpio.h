#ifndef BSP_GPIO_H
#define BSP_GPIO_H
#include <stdint.h>

/* 2026-09-21网表、DS12232 Rev5表12与RM0444 GPIO字段；仅拥有下列五脚。
 * 端口分组：PB14蜂鸣器；PC6继电器、PC7光耦、PC9报警灯；PD1电源灯。
 * 5V灯必须开漏且无内部上下拉；PD1为FT_d/UCPD2_DBCC1。 */
#define BSP_GPIO_RELAY_PIN       6U
#define BSP_GPIO_OPTO_PIN        7U
#define BSP_GPIO_BUZZER_PIN      14U
#define BSP_GPIO_WARNING_PIN     9U
#define BSP_GPIO_POWER_PIN       1U
#define BSP_GPIO_RELAY_ACTIVE    1U
#define BSP_GPIO_OPTO_ACTIVE     0U
#define BSP_GPIO_BUZZER_ACTIVE   1U
#define BSP_GPIO_WARNING_ACTIVE  0U
#define BSP_GPIO_POWER_ACTIVE    0U
#define BSP_GPIO_RELAY_OFF       (1U - BSP_GPIO_RELAY_ACTIVE)
#define BSP_GPIO_OPTO_OFF        (1U - BSP_GPIO_OPTO_ACTIVE)
#define BSP_GPIO_BUZZER_OFF      (1U - BSP_GPIO_BUZZER_ACTIVE)
#define BSP_GPIO_WARNING_OFF     (1U - BSP_GPIO_WARNING_ACTIVE)
#define BSP_GPIO_POWER_OFF       (1U - BSP_GPIO_POWER_ACTIVE)

int BSP_Gpio_Init(void);
int BSP_Gpio_IsInitialized(void);
/* 内部接口：调用者必须保存PRIMASK并屏蔽中断后调用。 */
int BSP_Gpio_CheckLocked(void);
void BSP_Gpio_OffLocked(void);
#endif
