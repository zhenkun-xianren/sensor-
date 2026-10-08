#include "bsp_gpio_board.h"
#include <stdbool.h>
static bool s_initialized = false; // 继电器GPIO最近一次初始化是否成功。
/** @brief 在调用者屏蔽中断时尽力将继电器PB8锁存置低。 */
void BSP_Gpio_OffLocked(void)
{
    if ((RCC->APB2ENR & BSP_GPIO_CLOCKS) != 0U)
        GPIOB->BSRR = BSP_GPIO_RELAY_MASK << BSP_GPIO_BSRR_RESET_SHIFT;
}
/**
 * @brief 在调用者屏蔽中断时核对PB8时钟和模式。
 * @return 正常返回BSP_GPIO_OK，否则返回BSP_GPIO_ERROR。
 */
int BSP_Gpio_CheckLocked(void)
{
    if (!s_initialized || (RCC->APB2ENR & BSP_GPIO_CLOCKS) != BSP_GPIO_CLOCKS ||
        (GPIOB->CRH & BSP_GPIO_RELAY_FIELDS) != BSP_GPIO_RELAY_MODE)
        return BSP_GPIO_ERROR;
    return BSP_GPIO_OK;
}
/**
 * @brief 查询PB8最近一次初始化结果，不表示负载反馈。
 * @return 初始化成功返回BSP_GPIO_HIGH，否则返回BSP_GPIO_LOW。
 */
int BSP_Gpio_IsInitialized(void)
{
    return s_initialized ? BSP_GPIO_HIGH : BSP_GPIO_LOW;
}
/**
 * @brief 仅初始化PB8，先装载关闭锁存再启用推挽输出。
 * @return 配置及锁存正常返回BSP_GPIO_OK，否则返回BSP_GPIO_ERROR。
 */
int BSP_Gpio_Init(void)
{
    uint32_t irq_state = __get_PRIMASK(); // 保存调用前中断屏蔽状态。
    int result = BSP_GPIO_ERROR; // 保存配置及锁存核对结果。
    __disable_irq();
    s_initialized = false;
    RCC->APB2ENR |= BSP_GPIO_CLOCKS;
    (void)RCC->APB2ENR;
    if ((RCC->APB2ENR & BSP_GPIO_CLOCKS) == BSP_GPIO_CLOCKS) {
        BSP_Gpio_OffLocked();
        GPIOB->CRH = (GPIOB->CRH & ~BSP_GPIO_RELAY_FIELDS) | BSP_GPIO_RELAY_MODE;
        s_initialized = true;
        if (BSP_Gpio_CheckLocked() == BSP_GPIO_OK &&
            (GPIOB->ODR & BSP_GPIO_RELAY_MASK) == 0U) result = BSP_GPIO_OK;
    }
    if (result != BSP_GPIO_OK) {
        s_initialized = false;
        BSP_Gpio_OffLocked();
    }
    __set_PRIMASK(irq_state);
    return result;
}
