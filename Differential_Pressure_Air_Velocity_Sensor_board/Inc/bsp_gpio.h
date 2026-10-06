#ifndef BSP_GPIO_H
#define BSP_GPIO_H
#include <stdint.h>
#include "bsp_board.h"
/* 正式引脚表V1.0：PB1继电器、PB15光耦、PA9蜂鸣器、PB13/PB14指示灯。 */
#define BSP_GPIO_RELAY_PIN 1U
#define BSP_GPIO_OPTO_PIN 15U
#define BSP_GPIO_BUZZER_PIN 9U
#define BSP_GPIO_WARNING_PIN 13U
#define BSP_GPIO_POWER_PIN 14U
#define BSP_GPIO_RELAY_ACTIVE BSP_BOARD_HIGH
#define BSP_GPIO_OPTO_ACTIVE BSP_BOARD_LOW
#define BSP_GPIO_BUZZER_ACTIVE BSP_BOARD_HIGH
#define BSP_GPIO_WARNING_ACTIVE BSP_BOARD_LOW
#define BSP_GPIO_POWER_ACTIVE BSP_BOARD_LOW
#define BSP_GPIO_RELAY_OFF BSP_BOARD_LOW
#define BSP_GPIO_OPTO_OFF BSP_BOARD_HIGH
#define BSP_GPIO_BUZZER_OFF BSP_BOARD_LOW
#define BSP_GPIO_WARNING_OFF BSP_BOARD_HIGH
#define BSP_GPIO_POWER_OFF BSP_BOARD_HIGH
/** @brief 先预置关闭锁存，再配置五路所属引脚；灯为开漏输出。
 * @return 成功BSP_BOARD_OK，否则BSP_BOARD_ERROR。 */
int BSP_Gpio_Init(void);
/** @brief 查询最近初始化结果。
 * @return 已初始化返回真，否则返回假。 */
int BSP_Gpio_IsInitialized(void);
/** @brief 屏蔽中断期间核对五路配置。
 * @return 配置正常BSP_BOARD_OK，否则BSP_BOARD_ERROR。 */
int BSP_Gpio_CheckLocked(void);
/** @brief 在调用者已屏蔽中断时尽力关闭所属五路锁存。 */
void BSP_Gpio_OffLocked(void);
#endif
