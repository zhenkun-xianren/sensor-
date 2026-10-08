#include "bsp_key.h"
#include <stdbool.h>
#include <stddef.h>

static bool s_key_initialized = false; // 所属GPIO是否通过初始化核对。

/** @brief 保存中断状态并进入短临界区。 @return 调用前PRIMASK。 */
uint32_t BSP_Key_EnterCritical(void)
{
    uint32_t saved_mask = __get_PRIMASK(); // 保存原有中断屏蔽状态。
    __disable_irq();
    __DMB();
    return saved_mask;
}

/** @brief 完成共享访问后恢复原中断状态。 @param saved_mask 调用前的PRIMASK。 */
void BSP_Key_ExitCritical(uint32_t saved_mask)
{
    __DMB();
    __set_PRIMASK(saved_mask);
}

/**
 * @brief 配置PA0/PA1/PA2浮空输入，由板上外部电阻维持释放高电平。
 * @return 寄存器读回一致返回BSP_KEY_OK，否则返回BSP_KEY_ERROR。
 */
int BSP_Key_Init(void)
{
    uint32_t saved_mask = BSP_Key_EnterCritical(); // 保护RCC与GPIO共享配置寄存器。
    int result = BSP_KEY_ERROR; // 保存本次配置核对结果。
    s_key_initialized = false;
    RCC->APB2ENR |= BSP_KEY_GPIO_CLOCK;
    /* 时钟使能读回形成总线延迟；不得对整个GPIO端口复位。 */
    if ((RCC->APB2ENR & BSP_KEY_GPIO_CLOCK) == BSP_KEY_GPIO_CLOCK) {
        GPIOA->CRL = (GPIOA->CRL & ~BSP_KEY_GPIO_FIELDS) | BSP_KEY_GPIO_FLOATING;
        if ((GPIOA->CRL & BSP_KEY_GPIO_FIELDS) == BSP_KEY_GPIO_FLOATING) {
            s_key_initialized = true;
            result = BSP_KEY_OK;
        }
    }
    BSP_Key_ExitCritical(saved_mask);
    return result;
}

/**
 * @brief 检查所属配置后一次读取三键，低电平转换为逻辑按下。
 * @param pressed_mask 保存按下位图的非空输出指针，位号对应DevKeyId。
 * @return 成功返回BSP_KEY_OK，否则返回BSP_KEY_ERROR且不改写输出。
 */
int BSP_Key_Read(uint32_t *pressed_mask)
{
    uint32_t input; // 同一时刻取得的GPIO输入快照。
    uint32_t mask = 0U; // 累计各路处于低电平的逻辑位。
    if (pressed_mask == NULL || !s_key_initialized) {
        return BSP_KEY_ERROR;
    }
    if ((RCC->APB2ENR & BSP_KEY_GPIO_CLOCK) != BSP_KEY_GPIO_CLOCK ||
        (GPIOA->CRL & BSP_KEY_GPIO_FIELDS) != BSP_KEY_GPIO_FLOATING) {
        return BSP_KEY_ERROR;
    }
    input = GPIOA->IDR;
    if ((input & BSP_KEY_SW6_GPIO_MASK) == 0U) mask |= BSP_KEY_SW6_STATE_MASK;
    if ((input & BSP_KEY_SW4_GPIO_MASK) == 0U) mask |= BSP_KEY_SW4_STATE_MASK;
    if ((input & BSP_KEY_SW5_GPIO_MASK) == 0U) mask |= BSP_KEY_SW5_STATE_MASK;
    *pressed_mask = mask;
    return BSP_KEY_OK;
}
