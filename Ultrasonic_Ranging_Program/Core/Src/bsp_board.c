#include "stm32g0xx.h"
#include "bsp_board.h"

/**
 * @brief 核对现有 64 MHz PLL 系统时钟和不分频 AHB/APB，不改写寄存器。
 * @return 0 表示一致，非 0 表示总线分频、时钟源或系统时钟不一致。
 */
int BSP_Board_CheckClock(void)
{
    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_1) {
        return -1;
    }
    /* 框架当前仅接受原工程 AHB/APB 不分频配置；读掩码核对，不重配时钟。 */
    if ((RCC->CFGR & (RCC_CFGR_HPRE | RCC_CFGR_PPRE)) != 0U) {
        return -1;
    }
    if (SystemCoreClock != BSP_BOARD_HCLK_HZ) {
        return -1;
    }
    return 0;
}
