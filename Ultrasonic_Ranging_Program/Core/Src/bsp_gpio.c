#include "bsp_gpio.h"
#include "bsp_gpio_board.h"

static uint8_t s_initialized; // 标记本单元是否完成所属引脚初始化。

/**
 * @brief 将可访问端口的所属输出锁存预置为全部关闭，不改变引脚模式。
 */
void BSP_Gpio_OffLocked(void)
{
    /* BSRR只写，低16位置位、高16位复位；禁止读改写。
     * 光耦关闭指现场输出低，因此PC7预置高；两灯高电平锁存表示高阻关闭。 */
    if ((RCC->IOPENR & RCC_IOPENR_GPIOBEN) != 0U) {
        GPIOB->BSRR = BSP_GPIO_B_HIGH | ((BSP_GPIO_B_MASK & ~BSP_GPIO_B_HIGH) << BSP_GPIO_BSRR_RESET_SHIFT);
    }
    if ((RCC->IOPENR & RCC_IOPENR_GPIOCEN) != 0U) {
        GPIOC->BSRR = BSP_GPIO_C_HIGH | ((BSP_GPIO_C_MASK & ~BSP_GPIO_C_HIGH) << BSP_GPIO_BSRR_RESET_SHIFT);
    }
    if ((RCC->IOPENR & RCC_IOPENR_GPIODEN) != 0U) {
        GPIOD->BSRR = BSP_GPIO_D_HIGH | ((BSP_GPIO_D_MASK & ~BSP_GPIO_D_HIGH) << BSP_GPIO_BSRR_RESET_SHIFT);
    }
}

/**
 * @brief 核对所属引脚配置及资源占用，调用者必须已屏蔽中断。
 * @return 0配置正常；-1表示未初始化、时钟关闭、UCPD2占用或引脚配置异常。
 */
int BSP_Gpio_CheckLocked(void)
{
    if (s_initialized == 0U || (RCC->IOPENR & BSP_GPIO_CLOCKS) != BSP_GPIO_CLOCKS ||
        (RCC->APBENR1 & RCC_APBENR1_UCPD2EN) != 0U) return -1;
    if ((GPIOB->MODER & BSP_GPIO_B_FIELDS) != BSP_GPIO_B_MODES ||
        (GPIOC->MODER & BSP_GPIO_C_FIELDS) != BSP_GPIO_C_MODES ||
        (GPIOD->MODER & BSP_GPIO_D_FIELDS) != BSP_GPIO_D_MODES ||
        (GPIOB->OTYPER & BSP_GPIO_B_MASK) != 0U ||
        (GPIOC->OTYPER & BSP_GPIO_C_MASK) != BSP_GPIO_C_OPEN_DRAIN ||
        (GPIOD->OTYPER & BSP_GPIO_D_MASK) != BSP_GPIO_D_OPEN_DRAIN ||
        (GPIOB->PUPDR & BSP_GPIO_B_FIELDS) != 0U ||
        (GPIOC->PUPDR & BSP_GPIO_C_FIELDS) != 0U ||
        (GPIOD->PUPDR & BSP_GPIO_D_FIELDS) != 0U ||
        (GPIOB->OSPEEDR & BSP_GPIO_B_FIELDS) != 0U ||
        (GPIOC->OSPEEDR & BSP_GPIO_C_FIELDS) != 0U ||
        (GPIOD->OSPEEDR & BSP_GPIO_D_FIELDS) != 0U) return -1;
    return 0;
}

/**
 * @brief 查询GPIO初始化状态，供执行器区分未初始化与运行配置错误。
 * @return 1表示GPIO初始化曾成功，0表示尚未成功或最近初始化失败。
 */
int BSP_Gpio_IsInitialized(void)
{
    return s_initialized != 0U ? 1 : 0;
}

/**
 * @brief 仅初始化五个执行器引脚，先预置关闭电平，再启用输出。
 * @return 0表示配置和默认锁存核对成功，-1表示资源或寄存器核对失败。
 */
int BSP_Gpio_Init(void)
{
    uint32_t irq_state = __get_PRIMASK(); // 保存调用前的中断屏蔽状态。
    int result = -1; // 保存初始化检查结果。
    __disable_irq();
    s_initialized = 0U;
    /* 不抢占已有UCPD2。该外设使用HSI16，参考既有红外BSP，以时钟门控保守判断占用。 */
    if ((RCC->APBENR1 & RCC_APBENR1_UCPD2EN) == 0U) {
        RCC->IOPENR |= BSP_GPIO_CLOCKS;
        RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
        (void)RCC->IOPENR;
        (void)RCC->APBENR2;
        if ((RCC->IOPENR & BSP_GPIO_CLOCKS) == BSP_GPIO_CLOCKS &&
            (RCC->APBENR2 & RCC_APBENR2_SYSCFGEN) != 0U) {
            BSP_Gpio_OffLocked();
            /* DS12232 Rev5：PC9为FT，PD1为FT_d；5V开漏应用禁止内部上下拉。
             * RM0444 Rev6 12.4.1：UCPD2关闭时STROBE解除默认死电池下拉。
             * 此共享控制也解除PD0/PD2默认CC下拉，不改其GPIO字段；显示/红外BSP仍独立配置。 */
            SYSCFG->CFGR1 |= SYSCFG_CFGR1_UCPD2_STROBE;
            GPIOB->OTYPER &= ~BSP_GPIO_B_MASK;
            GPIOC->OTYPER = (GPIOC->OTYPER & ~BSP_GPIO_C_MASK) | BSP_GPIO_C_OPEN_DRAIN;
            GPIOD->OTYPER = (GPIOD->OTYPER & ~BSP_GPIO_D_MASK) | BSP_GPIO_D_OPEN_DRAIN;
            GPIOB->OSPEEDR &= ~BSP_GPIO_B_FIELDS;
            GPIOC->OSPEEDR &= ~BSP_GPIO_C_FIELDS;
            GPIOD->OSPEEDR &= ~BSP_GPIO_D_FIELDS;
            GPIOB->PUPDR &= ~BSP_GPIO_B_FIELDS;
            GPIOC->PUPDR &= ~BSP_GPIO_C_FIELDS;
            GPIOD->PUPDR &= ~BSP_GPIO_D_FIELDS;
            /* 最后启用输出，避免先输出后调整电平产生启动脉冲。 */
            GPIOB->MODER = (GPIOB->MODER & ~BSP_GPIO_B_FIELDS) | BSP_GPIO_B_MODES;
            GPIOC->MODER = (GPIOC->MODER & ~BSP_GPIO_C_FIELDS) | BSP_GPIO_C_MODES;
            GPIOD->MODER = (GPIOD->MODER & ~BSP_GPIO_D_FIELDS) | BSP_GPIO_D_MODES;
            s_initialized = 1U;
            if (BSP_Gpio_CheckLocked() == 0 &&
                (GPIOB->ODR & BSP_GPIO_B_MASK) == BSP_GPIO_B_HIGH &&
                (GPIOC->ODR & BSP_GPIO_C_MASK) == BSP_GPIO_C_HIGH &&
                (GPIOD->ODR & BSP_GPIO_D_MASK) == BSP_GPIO_D_HIGH) result = 0;
        }
    }
    if (result != 0) {
        s_initialized = 0U;
        BSP_Gpio_OffLocked(); /* 只能尽力关闭输出锁存，不能保证实际负载已关闭。 */
    }
    __set_PRIMASK(irq_state);
    return result;
}
