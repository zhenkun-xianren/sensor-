#ifndef BSP_GPIO_H
#define BSP_GPIO_H
#include <stdint.h>
/* 正式引脚表V1.0：继电器PB8，经U97接Q13栅极；低边N沟道，高有效。 */
#define BSP_GPIO_RELAY_PIN 8U
#define BSP_GPIO_RELAY_ACTIVE 1U
#define BSP_GPIO_RELAY_OFF 0U
/**
 * @brief 仅初始化继电器PB8，先预置关闭锁存再启用2MHz推挽。
 * @return 成功返回0，配置或锁存异常返回-1。
 */
int BSP_Gpio_Init(void);
/**
 * @brief 查询继电器GPIO最近一次初始化结果。
 * @return 初始化成功返回1，否则返回0；不表示实际负载反馈。
 */
int BSP_Gpio_IsInitialized(void);
/**
 * @brief 调用者屏蔽中断时核对PB8时钟和模式。
 * @return 配置正常返回0，否则返回-1。
 */
int BSP_Gpio_CheckLocked(void);
/** @brief 调用者屏蔽中断时尽力关闭PB8输出锁存，不改变引脚模式。 */
void BSP_Gpio_OffLocked(void);
#endif
