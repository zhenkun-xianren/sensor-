#ifndef BSP_GPIO_H
#define BSP_GPIO_H
#include <stdint.h>

/* F103来源板卡五路映射；保留原极性，不代表实物已验证。
 * 寄存器依据：RM0008 Rev21 9.2.1至9.2.5；不配置其他功能或SWD引脚。 */
#define BSP_GPIO_RELAY_PIN       5U
#define BSP_GPIO_OPTO_PIN        1U
#define BSP_GPIO_BUZZER_PIN      6U
#define BSP_GPIO_WARNING_PIN     15U
#define BSP_GPIO_POWER_PIN       14U
#define BSP_GPIO_RELAY_ACTIVE    1U
#define BSP_GPIO_OPTO_ACTIVE     0U
#define BSP_GPIO_BUZZER_ACTIVE   1U
#define BSP_GPIO_WARNING_ACTIVE  1U
#define BSP_GPIO_POWER_ACTIVE    1U
#define BSP_GPIO_RELAY_OFF       (1U - BSP_GPIO_RELAY_ACTIVE)
#define BSP_GPIO_OPTO_OFF        (1U - BSP_GPIO_OPTO_ACTIVE)
#define BSP_GPIO_BUZZER_OFF      (1U - BSP_GPIO_BUZZER_ACTIVE)
#define BSP_GPIO_WARNING_OFF     (1U - BSP_GPIO_WARNING_ACTIVE)
#define BSP_GPIO_POWER_OFF       (1U - BSP_GPIO_POWER_ACTIVE)

/**
 * @brief 仅初始化来源板卡五路执行器GPIO，先预置关闭电平再启用输出。
 * @return 配置及关闭锁存核对成功返回0，否则返回-1。
 */
int BSP_Gpio_Init(void);
/**
 * @brief 查询五路GPIO最近一次初始化是否成功。
 * @return 已成功返回1，否则返回0；不代表当前配置或负载反馈。
 */
int BSP_Gpio_IsInitialized(void);
/**
 * @brief 在调用者已屏蔽中断时核对五路GPIO配置和时钟。
 * @return 配置正常返回0，未初始化或配置异常返回-1。
 */
int BSP_Gpio_CheckLocked(void);
/**
 * @brief 在调用者已屏蔽中断时尽力关闭可访问端口的五路输出锁存。
 */
void BSP_Gpio_OffLocked(void);
#endif
