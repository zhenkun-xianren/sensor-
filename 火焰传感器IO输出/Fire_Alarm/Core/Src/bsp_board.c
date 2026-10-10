#include "stm32f0xx.h"
#include "bsp_board.h"

static volatile uint32_t s_tick_ms; // 由 SysTick 中断更新的毫秒计数。

/**
 * @brief 显式配置 PA13/PA14 的 AF0 和上下拉，并读回核对。
 * @return 配置一致返回 OK，否则返回调试配置错误。
 */
static BspBoardStatus board_configure_swd(void)
{
    uint32_t mode = GPIO_MODER_MODER13_1 | GPIO_MODER_MODER14_1; // SWD 使用复用模式。
    uint32_t pull = GPIO_PUPDR_PUPDR13_0 | GPIO_PUPDR_PUPDR14_1; // SWDIO 上拉、SWCLK 下拉。
    uint32_t afMask = GPIO_AFRH_AFSEL13 | GPIO_AFRH_AFSEL14; // 仅修改两个调试引脚的 AF 字段。
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    (void)RCC->AHBENR; /* 总线读回，确保 GPIO 时钟已生效。 */
    GPIOA->AFR[1] &= ~afMask; /* AF0：STM32F051 数据手册表14；F051 仅支持 SWD，无 JTAG 关闭操作。 */
    GPIOA->OTYPER &= ~(GPIO_OTYPER_OT_13 | GPIO_OTYPER_OT_14);
    GPIOA->OSPEEDR = (GPIOA->OSPEEDR & ~(GPIO_OSPEEDR_OSPEEDR13 | GPIO_OSPEEDR_OSPEEDR14)) |
                       GPIO_OSPEEDR_OSPEEDR13 | GPIO_OSPEEDR_OSPEEDR14;
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(GPIO_PUPDR_PUPDR13 | GPIO_PUPDR_PUPDR14)) | pull;
    GPIOA->MODER = (GPIOA->MODER & ~(GPIO_MODER_MODER13 | GPIO_MODER_MODER14)) | mode;
    if (((RCC->AHBENR & RCC_AHBENR_GPIOAEN) != RCC_AHBENR_GPIOAEN) ||
        ((GPIOA->OTYPER & (GPIO_OTYPER_OT_13 | GPIO_OTYPER_OT_14)) != BSP_BOARD_FIELD_CLEAR) ||
        ((GPIOA->OSPEEDR & (GPIO_OSPEEDR_OSPEEDR13 | GPIO_OSPEEDR_OSPEEDR14)) !=
         (GPIO_OSPEEDR_OSPEEDR13 | GPIO_OSPEEDR_OSPEEDR14)) ||
        ((GPIOA->MODER & (GPIO_MODER_MODER13 | GPIO_MODER_MODER14)) != mode) ||
        ((GPIOA->PUPDR & (GPIO_PUPDR_PUPDR13 | GPIO_PUPDR_PUPDR14)) != pull) ||
        ((GPIOA->AFR[1] & afMask) != BSP_BOARD_SWD_AF)) {
        return BSP_BOARD_DEBUG_ERROR;
    }
    return BSP_BOARD_OK;
}

/**
 * @brief 使用板载8MHz无源晶振启动HSE，保持AHB/APB不分频并检查所有切换阶段。
 * @return 时钟符合板级定义返回BSP_BOARD_OK；任一阶段超时或读回错误返回BSP_BOARD_CLOCK_ERROR。
 */
static BspBoardStatus board_configure_clock(void)
{
    uint32_t remaining = BSP_BOARD_WAIT_LIMIT; // 时基建立前限制每一阶段的寄存器轮询次数。

    /* RM0091 6.4.1：复位使用HSI，先确保临时时钟可用再重设外部时钟。 */
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) != RCC_CR_HSIRDY) {
        if (remaining == 0U) {
            return BSP_BOARD_CLOCK_ERROR;
        }
        --remaining;
    }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
    remaining = BSP_BOARD_WAIT_LIMIT;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {
        if (remaining == 0U) {
            return BSP_BOARD_CLOCK_ERROR;
        }
        --remaining;
    }

    /* 本工程不使用PLL；只有退出PLL系统时钟后才可关闭PLL。 */
    RCC->CR &= ~RCC_CR_PLLON;
    remaining = BSP_BOARD_WAIT_LIMIT;
    while ((RCC->CR & RCC_CR_PLLRDY) != BSP_BOARD_FIELD_CLEAR) {
        if (remaining == 0U) {
            return BSP_BOARD_CLOCK_ERROR;
        }
        --remaining;
    }

    /* X2为无源晶振：HSE关闭并确认后清除旁路，使用PF0/PF1晶体振荡器。 */
    RCC->CR &= ~RCC_CR_HSEON;
    remaining = BSP_BOARD_WAIT_LIMIT;
    while ((RCC->CR & RCC_CR_HSERDY) != BSP_BOARD_FIELD_CLEAR) {
        if (remaining == 0U) {
            return BSP_BOARD_CLOCK_ERROR;
        }
        --remaining;
    }
    RCC->CR &= ~RCC_CR_HSEBYP;
    RCC->CR |= RCC_CR_HSEON;
    remaining = BSP_BOARD_WAIT_LIMIT;
    while ((RCC->CR & RCC_CR_HSERDY) != RCC_CR_HSERDY) {
        if (remaining == 0U) {
            return BSP_BOARD_CLOCK_ERROR;
        }
        --remaining;
    }

    /* RM0091 3.5.1：8MHz使用Flash零等待；AHB/APB均不分频。 */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | BSP_BOARD_FLASH_LATENCY;
    if ((FLASH->ACR & FLASH_ACR_LATENCY) != BSP_BOARD_FLASH_LATENCY) {
        return BSP_BOARD_CLOCK_ERROR;
    }
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE)) |
                RCC_CFGR_SW_HSE | RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE_DIV1;
    remaining = BSP_BOARD_WAIT_LIMIT;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSE) {
        if (remaining == 0U) {
            return BSP_BOARD_CLOCK_ERROR;
        }
        --remaining;
    }
    SystemCoreClockUpdate();
    if ((SystemCoreClock != BSP_BOARD_HCLK_HZ) ||
        ((RCC->CR & (RCC_CR_HSERDY | RCC_CR_HSEBYP)) != RCC_CR_HSERDY) ||
        ((RCC->CFGR & (RCC_CFGR_HPRE | RCC_CFGR_PPRE)) !=
         (RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE_DIV1))) {
        return BSP_BOARD_CLOCK_ERROR;
    }
    /* HSI保留开启，满足后续Flash擦写对内部时钟的要求；SYSCLK已确认为HSE。 */
    return BSP_BOARD_OK;
}

/**
 * @brief 先保护调试接口，再启动裸机时钟和毫秒计时。
 * @return 成功返回 OK，失败返回对应启动状态。
 */
BspBoardStatus BSP_Board_Init(void)
{
    BspBoardStatus result = board_configure_swd(); // 保存逐阶段启动结果。
    if (result != BSP_BOARD_OK) {
        return result;
    }
    result = board_configure_clock();
    if (result != BSP_BOARD_OK) {
        return result;
    }
    s_tick_ms = 0U;
    if (SysTick_Config(BSP_BOARD_HCLK_HZ / BSP_BOARD_TICK_HZ) != 0U) {
        return BSP_BOARD_TICK_ERROR;
    }
    NVIC_SetPriority(SysTick_IRQn, BSP_BOARD_SYSTICK_PRIORITY);
    return BSP_BOARD_OK;
}

/**
 * @brief 获取毫秒时间；无符号差值允许计数自然回绕。
 * @return 毫秒计数。
 */
uint32_t BSP_Board_GetMs(void)
{
    return s_tick_ms;
}

/** @brief 裸机系统计时中断，不调用 HAL 或 RTOS。 */
void SysTick_Handler(void)
{
    ++s_tick_ms;
}

/** @brief 停止后续初始化或业务，保留调试访问。 */
void BSP_Board_Fault(void)
{
    __disable_irq();
    for (;;) {
        __NOP();
    }
}
