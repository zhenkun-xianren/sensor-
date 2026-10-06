#include "bsp_gpio_board.h"
#include <stdbool.h>
static bool s_initialized = false; // 保存五路GPIO最近初始化是否成功。
/** @brief 尽力关闭五路锁存；光耦关闭语义为现场低电平，不表示负载实测。 */
void BSP_Gpio_OffLocked(void)
{
    if ((RCC->APB2ENR & RCC_APB2ENR_IOPAEN) != 0U) GPIOA->BSRR = BSP_GPIO_A_MASK << BSP_GPIO_BSRR_RESET_SHIFT;
    if ((RCC->APB2ENR & RCC_APB2ENR_IOPBEN) != 0U) GPIOB->BSRR = BSP_GPIO_B_HIGH |
        ((BSP_GPIO_B_MASK & ~BSP_GPIO_B_HIGH) << BSP_GPIO_BSRR_RESET_SHIFT);
}
/** @brief 核对五路GPIO所属字段及模式。
 * @return 配置正确BSP_BOARD_OK，否则BSP_BOARD_ERROR。 */
int BSP_Gpio_CheckLocked(void)
{
    return (s_initialized && (RCC->APB2ENR & BSP_GPIO_CLOCKS) == BSP_GPIO_CLOCKS &&
        (GPIOA->CRH & BSP_GPIO_A_FIELDS) == BSP_GPIO_A_MODES &&
        (GPIOB->CRL & BSP_GPIO_B_LOW_FIELDS) == BSP_GPIO_B_LOW_MODES &&
        (GPIOB->CRH & BSP_GPIO_B_FIELDS) == BSP_GPIO_B_MODES) ? BSP_BOARD_OK : BSP_BOARD_ERROR;
}
/** @brief 查询最近初始化状态。
 * @return 返回初始化成功的布尔值。 */
int BSP_Gpio_IsInitialized(void) { return s_initialized; }
/** @brief 五路GPIO初始化：先关闭锁存，再启用输出，不覆盖数码管和SWD。
 * @return 初始化及读回成功BSP_BOARD_OK，否则BSP_BOARD_ERROR。 */
int BSP_Gpio_Init(void)
{
    uint32_t saved = __get_PRIMASK(); // 保存原中断屏蔽状态。
    int result = BSP_BOARD_ERROR; // 保存配置读回结果。
    __disable_irq();
    s_initialized = false;
    RCC->APB2ENR |= BSP_GPIO_CLOCKS;
    (void)RCC->APB2ENR;
    BSP_Gpio_OffLocked();
    if (((GPIOA->LCKR & GPIO_LCKR_LCKK) == 0U || (GPIOA->LCKR & BSP_GPIO_A_MASK) == 0U) &&
        ((GPIOB->LCKR & GPIO_LCKR_LCKK) == 0U || (GPIOB->LCKR & BSP_GPIO_B_MASK) == 0U)) {
        GPIOA->CRH = (GPIOA->CRH & ~BSP_GPIO_A_FIELDS) | BSP_GPIO_A_MODES;
        GPIOB->CRL = (GPIOB->CRL & ~BSP_GPIO_B_LOW_FIELDS) | BSP_GPIO_B_LOW_MODES;
        GPIOB->CRH = (GPIOB->CRH & ~BSP_GPIO_B_FIELDS) | BSP_GPIO_B_MODES;
        s_initialized = true;
        if (BSP_Gpio_CheckLocked() == BSP_BOARD_OK && (GPIOA->ODR & BSP_GPIO_A_MASK) == BSP_GPIO_A_HIGH &&
            (GPIOB->ODR & BSP_GPIO_B_MASK) == BSP_GPIO_B_HIGH) result = BSP_BOARD_OK;
    }
    if (result != BSP_BOARD_OK) { s_initialized = false; BSP_Gpio_OffLocked(); }
    __set_PRIMASK(saved);
    return result;
}
