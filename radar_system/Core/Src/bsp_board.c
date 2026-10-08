#include "bsp_board.h"
#include "stm32f103xb.h"

/**
 * @brief 先建立调试配置，再保持原工程HSI直供及总线不分频，配置1ms基础时钟。
 * @return 可读配置核对成功返回BSP_BOARD_OK，失败返回调试、时钟或计时错误。
 * @details RM0008第184页SWJ_CFG只写、读值未定义，不能读回验证该字段；
 * 显式写010关闭JTAG保留SWD，并读回GPIO电气字段。实物调试连接由用户验证。
 */
BspBoardResult BSP_Board_Init(void)
{
    uint32_t saved_mask = __get_PRIMASK(); // 保存启动前中断状态。
    uint32_t remap; // 保留MAPR中其他模块可读的重映射配置。
    uint32_t wait_count; // 用于限制无基础时钟时的硬件就绪等待。
    __disable_irq();
    RCC->APB2ENR |= BOARD_DEBUG_CLOCKS;
    if ((RCC->APB2ENR & BOARD_DEBUG_CLOCKS) != BOARD_DEBUG_CLOCKS) {
        __set_PRIMASK(saved_mask);
        return BSP_BOARD_DEBUG_ERROR;
    }
    /* SWDIO上拉、SWCLK下拉与调试专用复用共存，不覆盖其他端口字段。 */
    GPIOA->BSRR = GPIO_BSRR_BS13 | GPIO_BSRR_BR14;
    GPIOA->CRH = (GPIOA->CRH & ~BOARD_DEBUG_FIELDS) | BOARD_DEBUG_INPUT_PULL;
    remap = AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG;
    AFIO->MAPR = remap | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
    __DSB();
    if ((GPIOA->CRH & BOARD_DEBUG_FIELDS) != BOARD_DEBUG_INPUT_PULL ||
        (GPIOA->ODR & BOARD_DEBUG_PULL_MASK) != BOARD_DEBUG_PULL_VALUE) {
        __set_PRIMASK(saved_mask);
        return BSP_BOARD_DEBUG_ERROR;
    }
    /* 保留既有8MHz配置；HSITRIM=16与生成参考SystemClock_Config一致。 */
    RCC->CR |= RCC_CR_HSION;
    for (wait_count = 0U; wait_count < BSP_BOARD_CLOCK_WAIT_LIMIT; ++wait_count) {
        if ((RCC->CR & RCC_CR_HSIRDY) != 0U) break;
    }
    if (wait_count == BSP_BOARD_CLOCK_WAIT_LIMIT) {
        __set_PRIMASK(saved_mask);
        return BSP_BOARD_CLOCK_ERROR;
    }
    RCC->CR = (RCC->CR & ~RCC_CR_HSITRIM) |
        (BSP_BOARD_HSI_TRIM_DEFAULT << RCC_CR_HSITRIM_Pos);
    RCC->CFGR = (RCC->CFGR & ~BOARD_CLOCK_FIELDS) | BOARD_CLOCK_HSI_UNDIVIDED;
    for (wait_count = 0U; wait_count < BSP_BOARD_CLOCK_WAIT_LIMIT; ++wait_count) {
        if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_HSI) break;
    }
    if (wait_count == BSP_BOARD_CLOCK_WAIT_LIMIT) {
        __set_PRIMASK(saved_mask);
        return BSP_BOARD_CLOCK_ERROR;
    }
    /* 系统已使用HSI，8MHz允许0等待周期；不修改外设引脚或重映射。 */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | BOARD_FLASH_ZERO_WAIT;
    RCC->CR &= ~RCC_CR_PLLON;
    SystemCoreClockUpdate();
    if ((RCC->CFGR & BOARD_CLOCK_FIELDS) != BOARD_CLOCK_HSI_UNDIVIDED ||
        SystemCoreClock != BSP_BOARD_HCLK_HZ) {
        __set_PRIMASK(saved_mask);
        return BSP_BOARD_CLOCK_ERROR;
    }
    NVIC_SetPriorityGrouping(BSP_BOARD_PRIORITY_GROUP);
    SCB->VTOR = FLASH_BASE;
    if (SysTick_Config(BSP_BOARD_HCLK_HZ / BSP_BOARD_TICK_HZ) != BSP_BOARD_SYSTICK_OK) {
        __set_PRIMASK(saved_mask);
        return BSP_BOARD_TIME_ERROR;
    }
    __set_PRIMASK(saved_mask);
    return BSP_BOARD_OK;
}
