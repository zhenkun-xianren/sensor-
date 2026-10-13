#include "stm32g0xx.h"
#include "bsp_startup.h"
#include "bsp_board.h"
#include "bsp_time.h"
#include "bsp_fault.h"

/**
 * @brief 显式配置PA13/SWDIO与PA14/SWCLK，读回核对并保留其他GPIOA引脚。
 * @return 配置及核对完成返回0，GPIOA时钟或调试引脚字段核对失败返回-1。
 * @note STM32G071只提供SW-DP，无需且不得照搬其他芯片的JTAG关闭寄存器操作。
 */
static int startup_debug_swd_init(void)
{
    uint32_t irq_state = __get_PRIMASK(); // 保存进入短临界区前的中断屏蔽状态。
    int result = -1; // 保存GPIOA时钟及SWD寄存器字段的核对结果。

    __disable_irq();
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    (void)RCC->IOPENR; /* 时钟使能读回后才能访问GPIOA配置寄存器。 */
    if ((RCC->IOPENR & RCC_IOPENR_GPIOAEN) != 0U) {
        /* 仅更新两脚所属字段，不整端口复位、不锁定GPIO、不操作选项字节。
         * 保持复用模式期间先建立AF0和电气属性，避免把SWD临时切成GPIO或模拟模式。 */
        GPIOA->AFR[1] = (GPIOA->AFR[1] & ~BSP_STARTUP_SWD_AF_MASK) |
                        BSP_STARTUP_SWD_AF_BITS;
        GPIOA->OTYPER &= ~BSP_STARTUP_SWD_PIN_MASK; /* SWDIO由调试外设控制收发方向。 */
        GPIOA->OSPEEDR = (GPIOA->OSPEEDR & ~BSP_STARTUP_SWD_SPEED_MASK) |
                         BSP_STARTUP_SWD_SPEED_BITS;
        GPIOA->PUPDR = (GPIOA->PUPDR & ~BSP_STARTUP_SWD_PULL_MASK) |
                       BSP_STARTUP_SWD_PULL_BITS; /* SWDIO上拉、SWCLK下拉，保持复位偏置。 */
        GPIOA->MODER = (GPIOA->MODER & ~BSP_STARTUP_SWD_MODE_MASK) |
                       BSP_STARTUP_SWD_MODE_BITS;
        __DSB(); /* 确认寄存器写入完成后读回；后续初始化不得覆盖这两个调试引脚。 */
        if ((GPIOA->AFR[1] & BSP_STARTUP_SWD_AF_MASK) == BSP_STARTUP_SWD_AF_BITS &&
            (GPIOA->OTYPER & BSP_STARTUP_SWD_PIN_MASK) == 0U &&
            (GPIOA->OSPEEDR & BSP_STARTUP_SWD_SPEED_MASK) == BSP_STARTUP_SWD_SPEED_BITS &&
            (GPIOA->PUPDR & BSP_STARTUP_SWD_PULL_MASK) == BSP_STARTUP_SWD_PULL_BITS &&
            (GPIOA->MODER & BSP_STARTUP_SWD_MODE_MASK) == BSP_STARTUP_SWD_MODE_BITS) {
            result = 0;
        }
    }
    __set_PRIMASK(irq_state);
    return result;
}

/**
 * @brief 在启动阶段有限轮询寄存器状态，不依赖尚未建立的毫秒中断。
 * @param reg 待核对的寄存器地址。
 * @param mask 要核对的位掩码。
 * @param expected 掩码内预期的字段值。
 * @return 字段一致返回0，超过启动轮询预算返回-1。
 */
static int startup_wait(volatile uint32_t *reg, uint32_t mask, uint32_t expected)
{
    uint32_t remaining = BSP_STARTUP_POLL_LIMIT; // 限制等待次数，不把循环次数冒称为毫秒。
    while (remaining != 0U) {
        if ((*reg & mask) == expected) {
            return 0;
        }
        --remaining;
        __NOP();
    }
    return -1;
}

/**
 * @brief 根据RM0444 Rev6第75、118及174至182页建立原64MHz时钟配置。
 * @return 配置及核对完成返回0，电源、Flash或时钟等待失败返回-1。
 */
static int startup_clock_init(void)
{
    /* PWR配置前使能APB访问；SYSCFG为既有基础配置，未启用新的外设功能。 */
    RCC->APBENR1 |= RCC_APBENR1_PWREN;
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    (void)RCC->APBENR1;
    (void)RCC->APBENR2;
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_VOS) | PWR_CR1_VOS_0;
    if (startup_wait(&PWR->SR2, PWR_SR2_VOSF, 0U) != 0) {
        return -1;
    }
    RCC->CR = (RCC->CR & ~RCC_CR_HSIDIV) | RCC_CR_HSION;
    RCC->ICSCR = (RCC->ICSCR & ~RCC_ICSCR_HSITRIM) |
                 (BSP_BOARD_HSI_TRIM_DEFAULT << RCC_ICSCR_HSITRIM_Pos);
    if (startup_wait(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY) != 0) {
        return -1;
    }
    /* 先确认系统已用HSI，再关闭PLL；不可在PLL作为SYSCLK时改写PLL分频。 */
    RCC->CFGR &= ~RCC_CFGR_SW;
    if (startup_wait(&RCC->CFGR, RCC_CFGR_SWS, 0U) != 0) {
        return -1;
    }
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE);
    RCC->CR &= ~RCC_CR_PLLON;
    if (startup_wait(&RCC->CR, RCC_CR_PLLRDY, 0U) != 0) {
        return -1;
    }
    /* M/P/Q/R字段编码为除数减1，N为倍频值本身；仅启用原SYSCLK所需R输出。 */
    RCC->PLLCFGR = (RCC->PLLCFGR & ~(RCC_PLLCFGR_PLLSRC | RCC_PLLCFGR_PLLM |
                        RCC_PLLCFGR_PLLN | RCC_PLLCFGR_PLLP | RCC_PLLCFGR_PLLQ |
                        RCC_PLLCFGR_PLLR | RCC_PLLCFGR_PLLPEN | RCC_PLLCFGR_PLLQEN |
                        RCC_PLLCFGR_PLLREN)) |
                    RCC_PLLCFGR_PLLSRC_HSI |
                    ((BSP_BOARD_PLL_M - 1UL) << RCC_PLLCFGR_PLLM_Pos) |
                    (BSP_BOARD_PLL_N << RCC_PLLCFGR_PLLN_Pos) |
                    ((BSP_BOARD_PLL_P - 1UL) << RCC_PLLCFGR_PLLP_Pos) |
                    ((BSP_BOARD_PLL_Q - 1UL) << RCC_PLLCFGR_PLLQ_Pos) |
                    ((BSP_BOARD_PLL_R - 1UL) << RCC_PLLCFGR_PLLR_Pos) |
                    RCC_PLLCFGR_PLLREN;
    /* Range1在64MHz需要2个等待周期，先读回确认，再提频；保留原预取和指令缓存。 */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) |
                 (BSP_BOARD_FLASH_WAIT_STATES << FLASH_ACR_LATENCY_Pos) |
                 FLASH_ACR_PRFTEN | FLASH_ACR_ICEN;
    if (startup_wait(&FLASH->ACR, FLASH_ACR_LATENCY,
                    BSP_BOARD_FLASH_WAIT_STATES << FLASH_ACR_LATENCY_Pos) != 0) {
        return -1;
    }
    RCC->CR |= RCC_CR_PLLON;
    if (startup_wait(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY) != 0) {
        return -1;
    }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_1;
    if (startup_wait(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_1) != 0) {
        return -1;
    }
    __DSB();
    __ISB();
    SystemCoreClockUpdate();
    return BSP_Board_CheckClock();
}

/**
 * @brief 保留启动阶段系统时钟配置接口；寄存器配置失败时明确记录时钟故障。
 * @note 仅用于调度器启动前，不支持运行期动态切换时钟。
 */
void SystemClock_Config(void)
{
    if (startup_clock_init() != 0) {
        BSP_Fault_Stop(BSP_FAULT_CLOCK, __FILE__, (uint32_t)__LINE__);
    }
}

/**
 * @brief 保留原USART1 PC4/PC5参考配置；宏默认关闭，不能与C2/COMP_3功能同时启用。
 * @return 收发使能确认返回0，有限等待失败返回-1且关闭USART1。
 */
#if BSP_STARTUP_ENABLE_LEGACY_USART1
static int startup_usart1_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOCEN | RCC_IOPENR_GPIOAEN;
    RCC->CCIPR &= ~RCC_CCIPR_USART1SEL; /* 00为PCLK，不借用HSI或改变串口输入时钟。 */
    RCC->APBENR2 |= RCC_APBENR2_USART1EN;
    (void)RCC->IOPENR;
    (void)RCC->APBENR2;
    GPIOC->OTYPER &= ~BSP_STARTUP_USART1_PIN_MASK;
    GPIOC->PUPDR &= ~BSP_STARTUP_USART1_MODE_MASK;
    GPIOC->OSPEEDR &= ~BSP_STARTUP_USART1_MODE_MASK;
    GPIOC->AFR[0] = (GPIOC->AFR[0] & ~BSP_STARTUP_USART1_AF_MASK) |
                    BSP_STARTUP_USART1_AF_BITS;
    GPIOC->MODER = (GPIOC->MODER & ~BSP_STARTUP_USART1_MODE_MASK) |
                   BSP_STARTUP_USART1_MODE_BITS;
    USART1->CR1 = 0U; /* UE=0后设置格式；M/PCE/OVER8/FIFOEN为0，即8N1、16倍采样及无FIFO。 */
    USART1->CR2 = 0U;
    USART1->CR3 = 0U; /* 无硬件流控、DMA或中断，FIFO阈值复位字段即原1/8。 */
    USART1->PRESC = 0U; /* PRESCALER=0000，输入时钟不分频。 */
    USART1->BRR = BSP_STARTUP_USART1_BRR;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
    if (startup_wait(&USART1->ISR, USART_ISR_TEACK | USART_ISR_REACK,
                     USART_ISR_TEACK | USART_ISR_REACK) != 0) {
        USART1->CR1 = 0U;
        return -1;
    }
    return 0;
}

#endif /* BSP_STARTUP_ENABLE_LEGACY_USART1：保留参考实现，正式目标不初始化。 */

/**
 * @brief 依次完成SWD、寄存器时钟和独立SysTick，可选残留USART1默认屏蔽。
 * @return 成功返回BSP_STARTUP_OK；SWD、时钟、基础计时或串口失败返回对应状态。
 */
BspStartupStatus BSP_Startup_Init(void)
{
    /* 本入口只在复位启动阶段调用；关闭旧SysTick避免时钟切换期间错误计时。 */
    SysTick->CTRL = 0U;
    if (startup_debug_swd_init() != 0) {
        return BSP_STARTUP_DEBUG_FAILED;
    }
    if (startup_clock_init() != 0) {
        return BSP_STARTUP_CLOCK_FAILED;
    }
    NVIC_SetPriority(PendSV_IRQn, BSP_BOARD_KERNEL_IRQ_PRIORITY);
    if (BSP_Time_Init() != 0) {
        return BSP_STARTUP_BASE_FAILED;
    }
#if BSP_STARTUP_ENABLE_LEGACY_USART1
    if (startup_usart1_init() != 0) {
        return BSP_STARTUP_PERIPHERAL_FAILED;
    }
#endif
    return BSP_STARTUP_OK;
}
