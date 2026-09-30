#include "bsp_board.h"
#include "stm32f103xb.h"

/**
 * @brief 在有限轮询次数内等待寄存器状态到达指定值。
 * @param reg 待读取的外设寄存器地址。
 * @param mask 需要检查的状态位掩码。
 * @param expected 掩码内期望的状态值。
 * @return 匹配返回 1；超时返回 0。
 */
static uint8_t wait_register_state(volatile uint32_t *reg, uint32_t mask,
                                   uint32_t expected)
{
    uint32_t remaining = BSP_BOARD_CLOCK_WAIT_LIMIT; // 等待硬件状态变化的剩余轮询次数。

    while ((*reg & mask) != expected) {
        if (remaining == 0U) {
            return 0U;
        }
        --remaining;
    }
    return 1U;
}

/**
 * @brief 用寄存器配置与已验证 LEDTest 一致的 HSI 64 MHz 时钟。
 * @return 成功返回 BSP_BOARD_CLOCK_OK；失败返回对应阶段的错误状态。
 */
BspBoardClockStatus BSP_Board_ClockInit(void)
{
    uint32_t cfgr; // 时钟配置寄存器的新值，保留与本次时钟树无关的位。

    RCC->CR |= RCC_CR_HSION;
    if (wait_register_state(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY) == 0U) {
        return BSP_BOARD_CLOCK_HSI_TIMEOUT;
    }

    /* 重设 PLL 前先切回 HSI，避免在系统仍使用 PLL 时关闭它。 */
    RCC->CFGR &= ~RCC_CFGR_SW;
    if (wait_register_state(&RCC->CFGR, RCC_CFGR_SWS,
                            RCC_CFGR_SWS_HSI) == 0U) {
        return BSP_BOARD_CLOCK_SWITCH_HSI_TIMEOUT;
    }
    RCC->CR &= ~RCC_CR_PLLON;
    if (wait_register_state(&RCC->CR, RCC_CR_PLLRDY, 0U) == 0U) {
        return BSP_BOARD_CLOCK_PLL_STOP_TIMEOUT;
    }

    /* 64 MHz 需要两个 Flash 等待周期；切换到高速时钟前先配置。 */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) |
                 FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;
    if ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_2) {
        return BSP_BOARD_CLOCK_FLASH_LATENCY_ERROR;
    }

    /* HSI/2 × 16 = 64 MHz；AHB 不分频，APB1 /2，APB2 不分频。 */
    cfgr = RCC->CFGR;
    cfgr &= ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE1 |
              RCC_CFGR_PPRE2 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE |
              RCC_CFGR_PLLMULL);
    cfgr |= RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PLLMULL16;
    RCC->CFGR = cfgr;

    RCC->CR |= RCC_CR_PLLON;
    if (wait_register_state(&RCC->CR, RCC_CR_PLLRDY,
                            RCC_CR_PLLRDY) == 0U) {
        return BSP_BOARD_CLOCK_PLL_START_TIMEOUT;
    }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    if (wait_register_state(&RCC->CFGR, RCC_CFGR_SWS,
                            RCC_CFGR_SWS_PLL) == 0U) {
        return BSP_BOARD_CLOCK_SWITCH_PLL_TIMEOUT;
    }

    SystemCoreClockUpdate();
    return SystemCoreClock == BSP_BOARD_SYSCLK_HZ ? BSP_BOARD_CLOCK_OK :
           BSP_BOARD_CLOCK_FREQUENCY_ERROR;
}
