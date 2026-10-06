#include "bsp_gpio.h"
#include "bsp_gpio_board.h"

static uint8_t s_initialized; // 标记五路执行器GPIO最近一次初始化结果。

/**
 * @brief 在调用者已屏蔽中断时尽力预置五路关闭锁存，不改变引脚模式。
 */
void BSP_Gpio_OffLocked(void)
{
    /* BSRR只写；只操作已开时钟的所属引脚，不保证真实负载已关闭。 */
    if ((RCC->APB2ENR & RCC_APB2ENR_IOPAEN) != 0U) {
        GPIOA->BSRR = BSP_GPIO_A_HIGH | ((BSP_GPIO_A_MASK & ~BSP_GPIO_A_HIGH) << BSP_GPIO_BSRR_RESET_SHIFT);
    }
    if ((RCC->APB2ENR & RCC_APB2ENR_IOPBEN) != 0U) {
        GPIOB->BSRR = BSP_GPIO_B_HIGH | ((BSP_GPIO_B_MASK & ~BSP_GPIO_B_HIGH) << BSP_GPIO_BSRR_RESET_SHIFT);
    }
}

/**
 * @brief 在调用者已屏蔽中断时核对五路GPIO的时钟及2MHz推挽配置。
 * @return 已初始化且配置正常返回0，否则返回-1。
 */
int BSP_Gpio_CheckLocked(void)
{
    if (s_initialized == 0U || (RCC->APB2ENR & BSP_GPIO_CLOCKS) != BSP_GPIO_CLOCKS) return -1;
    if ((GPIOA->CRL & BSP_GPIO_A_FIELDS) != BSP_GPIO_A_MODES ||
        (GPIOB->CRH & BSP_GPIO_B_FIELDS) != BSP_GPIO_B_MODES) return -1;
    return 0;
}

/**
 * @brief 查询最近一次五路GPIO初始化是否成功。
 * @return 初始化曾成功返回1，尚未成功或最近失败返回0。
 */
int BSP_Gpio_IsInitialized(void)
{
    return s_initialized != 0U ? 1 : 0;
}

/**
 * @brief 仅初始化五路执行器GPIO，保留其他引脚及既有SWD配置。
 * @return 配置和默认关闭锁存核对成功返回0，失败返回-1。
 */
int BSP_Gpio_Init(void)
{
    uint32_t irq_state = __get_PRIMASK(); // 保存调用前中断屏蔽状态。
    int result = -1; // 保存五路初始化及读回结果。
    __disable_irq();
    s_initialized = 0U;
    RCC->APB2ENR |= BSP_GPIO_CLOCKS;
    (void)RCC->APB2ENR;
    if ((RCC->APB2ENR & BSP_GPIO_CLOCKS) == BSP_GPIO_CLOCKS) {
        /* 先装载安全锁存，避免启用输出时出现短暂开启；PC817关闭对应PA1高。 */
        BSP_Gpio_OffLocked();
        GPIOA->CRL = (GPIOA->CRL & ~BSP_GPIO_A_FIELDS) | BSP_GPIO_A_MODES;
        GPIOB->CRH = (GPIOB->CRH & ~BSP_GPIO_B_FIELDS) | BSP_GPIO_B_MODES;
        s_initialized = 1U;
        if (BSP_Gpio_CheckLocked() == 0 &&
            (GPIOA->ODR & BSP_GPIO_A_MASK) == BSP_GPIO_A_HIGH &&
            (GPIOB->ODR & BSP_GPIO_B_MASK) == BSP_GPIO_B_HIGH) result = 0;
    }
    if (result != 0) {
        s_initialized = 0U;
        BSP_Gpio_OffLocked();
    }
    __set_PRIMASK(irq_state);
    return result;
}
