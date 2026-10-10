#include <stdbool.h>
#include "bsp_gpio.h"
#include "bsp_gpio_board.h"

static bool s_initialized = false; // 标记光耦GPIO最近一次初始化结果。

/** @brief 在调用者已屏蔽中断时设置光耦默认锁存，不改变引脚模式。 */
void BSP_Gpio_OffLocked(void)
{
    if ((RCC->AHBENR & BSP_GPIO_CLOCKS) == BSP_GPIO_CLOCKS) {
        GPIOA->BSRR = BSP_GPIO_A_HIGH; /* PA8高：PC817导通，现场输出低。 */
    }
}

/** @brief 在调用者已屏蔽中断时核对光耦GPIO配置。
 * @return 已初始化且推挽、低速、无上下拉返回OK，否则ERROR。 */
int BSP_Gpio_CheckLocked(void)
{
    if (!s_initialized || (RCC->AHBENR & BSP_GPIO_CLOCKS) != BSP_GPIO_CLOCKS) {
        return BSP_GPIO_ERROR;
    }
    if (((GPIOA->MODER & BSP_GPIO_A_FIELDS) != BSP_GPIO_A_MODES) ||
        ((GPIOA->OTYPER & BSP_GPIO_OPTO_TYPE) != BSP_GPIO_FIELD_CLEAR) ||
        ((GPIOA->OSPEEDR & BSP_GPIO_OPTO_SPEED) != BSP_GPIO_FIELD_CLEAR) ||
        ((GPIOA->PUPDR & BSP_GPIO_OPTO_PULL) != BSP_GPIO_FIELD_CLEAR)) {
        return BSP_GPIO_ERROR;
    }
    return BSP_GPIO_OK;
}

/** @brief 查询最近一次初始化结果。
 * @return 已初始化返回INITIALIZED，否则NOT_INITIALIZED。 */
int BSP_Gpio_IsInitialized(void)
{
    return s_initialized ? BSP_GPIO_INITIALIZED : BSP_GPIO_NOT_INITIALIZED;
}

/** @brief 仅配置光耦PA8，并核对默认现场低电平锁存。
 * @return 初始化成功返回OK，否则ERROR。 */
int BSP_Gpio_Init(void)
{
    uint32_t irq_state = __get_PRIMASK(); // 保存调用前中断屏蔽状态。
    int result = BSP_GPIO_ERROR; // 保存配置及读回结果。
    __disable_irq();
    s_initialized = false;
    RCC->AHBENR |= BSP_GPIO_CLOCKS;
    (void)RCC->AHBENR;
    if ((RCC->AHBENR & BSP_GPIO_CLOCKS) == BSP_GPIO_CLOCKS) {
        BSP_Gpio_OffLocked();
        GPIOA->OTYPER &= ~BSP_GPIO_OPTO_TYPE;
        GPIOA->OSPEEDR &= ~BSP_GPIO_OPTO_SPEED;
        GPIOA->PUPDR &= ~BSP_GPIO_OPTO_PULL;
        GPIOA->MODER = (GPIOA->MODER & ~BSP_GPIO_A_FIELDS) | BSP_GPIO_A_MODES;
        s_initialized = true;
        if ((BSP_Gpio_CheckLocked() == BSP_GPIO_OK) &&
            ((GPIOA->ODR & BSP_GPIO_A_MASK) == BSP_GPIO_A_HIGH)) {
            result = BSP_GPIO_OK;
        }
    }
    if (result != BSP_GPIO_OK) {
        s_initialized = false;
        BSP_Gpio_OffLocked();
    }
    __set_PRIMASK(irq_state);
    return result;
}
