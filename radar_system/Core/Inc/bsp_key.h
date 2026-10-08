#ifndef BSP_KEY_H
#define BSP_KEY_H

#include <stdint.h>
#include "dev_key.h"
#include "stm32f103xb.h"

/* 正式引脚表V1.1：SW6=PA0、SW4=PA1、SW5=PA2。
 * 用户确认外部上拉；按下拉低，采用MODE=00/CNF=01浮空输入。
 * RM0008 Rev21第171页；只改三个四位字段，保留其他模块配置。 */
#define BSP_KEY_GPIO_CLOCK          RCC_APB2ENR_IOPAEN
#define BSP_KEY_GPIO_FIELDS         (GPIO_CRL_MODE0 | GPIO_CRL_CNF0 | \
                                    GPIO_CRL_MODE1 | GPIO_CRL_CNF1 | \
                                    GPIO_CRL_MODE2 | GPIO_CRL_CNF2)
#define BSP_KEY_GPIO_FLOATING       (GPIO_CRL_CNF0_0 | GPIO_CRL_CNF1_0 | GPIO_CRL_CNF2_0)
#define BSP_KEY_SW6_GPIO_MASK        GPIO_IDR_IDR0
#define BSP_KEY_SW4_GPIO_MASK        GPIO_IDR_IDR1
#define BSP_KEY_SW5_GPIO_MASK        GPIO_IDR_IDR2
#define BSP_KEY_SW6_STATE_MASK       (1UL << DEV_KEY_SW6)
#define BSP_KEY_SW4_STATE_MASK       (1UL << DEV_KEY_SW4)
#define BSP_KEY_SW5_STATE_MASK       (1UL << DEV_KEY_SW5)
enum {
    BSP_KEY_OK = 0, // 板级操作成功。
    BSP_KEY_ERROR = -1 // 参数、初始化或配置核对失败。
};

/** @brief 仅初始化三路按键输入并读回核对。 @return 成功返回BSP_KEY_OK，失败返回BSP_KEY_ERROR。 */
int BSP_Key_Init(void);
/**
 * @brief 读取一次GPIO快照并转换为按下位图。
 * @param pressed_mask 非空输出指针；位号对应DevKeyId，置位表示按下。
 * @return 成功返回BSP_KEY_OK，配置或参数错误返回BSP_KEY_ERROR，不改写输出。
 */
int BSP_Key_Read(uint32_t *pressed_mask);
/** @brief 保存并屏蔽中断，保护共享寄存器及驱动状态。 @return 调用前PRIMASK。 */
uint32_t BSP_Key_EnterCritical(void);
/** @brief 恢复原有中断屏蔽状态。 @param saved_mask 配套进入接口返回的PRIMASK。 */
void BSP_Key_ExitCritical(uint32_t saved_mask);

#endif
