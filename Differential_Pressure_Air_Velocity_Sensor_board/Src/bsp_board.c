#include "bsp_board.h"
#include "bsp_gpio.h"
#include "bsp_display.h"
#include "stm32f103xb.h"
#include "FreeRTOS.h"
#include "task.h"
volatile BspBoardFault g_board_fault = BSP_FAULT_NONE; // 保存固件最近故障阶段。
static volatile uint32_t s_milliseconds; // 保存启动前后连续的1ms计数。
/** @brief 配置SWD引脚并关闭JTAG；SWJ_CFG只写字段不作虚假读回比较。
 * @return 可读配置正常返回BSP_BOARD_OK，否则BSP_BOARD_ERROR。 */
int BSP_Board_DebugInit(void)
{
    uint32_t mapr; // 保存可读的AFIO重映射字段。
    uint32_t fields = GPIO_CRH_MODE13 | GPIO_CRH_CNF13 | GPIO_CRH_MODE14 | GPIO_CRH_CNF14; // 限定SWD两脚字段。
    uint32_t modes = GPIO_CRH_CNF13_1 | GPIO_CRH_CNF14_1; // SWDIO上拉、SWCLK下拉输入配置。
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN;
    (void)RCC->APB2ENR;
    GPIOA->BSRR = GPIO_BSRR_BS13 | GPIO_BSRR_BR14;
    GPIOA->CRH = (GPIOA->CRH & ~fields) | modes;
    /* RM0008 Rev21 p184：SWJ_CFG读值未定义；每次MAPR写入明确附带SWD模式。 */
    mapr = AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG;
    AFIO->MAPR = mapr | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
    __DSB();
    return ((RCC->APB2ENR & (RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN)) ==
        (RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN) &&
        (GPIOA->CRH & fields) == modes && (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG) == mapr)
        ? BSP_BOARD_OK : BSP_BOARD_ERROR;
}
/** @brief 核对内部HSI直接供系统时钟、三个总线不分频及HSE/PLL关闭配置。
 * @return 匹配BSP_BOARD_OK，否则BSP_BOARD_ERROR。 */
int BSP_Board_CheckClock(void)
{
    uint32_t fields = RCC_CFGR_SWS | RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2; // 保存需要核对的时钟字段。
    uint32_t oscillator = RCC_CR_HSION | RCC_CR_HSIRDY | RCC_CR_HSEON | RCC_CR_HSERDY |
        RCC_CR_HSEBYP | RCC_CR_PLLON | RCC_CR_PLLRDY | RCC_CR_CSSON; // 核对HSI就绪及外部时钟、旁路、PLL与CSS关闭。
    return ((RCC->CR & oscillator) == (RCC_CR_HSION | RCC_CR_HSIRDY) &&
        (RCC->CFGR & fields) == RCC_CFGR_SWS_HSI &&
        SystemCoreClock == BSP_BOARD_HCLK_HZ) ? BSP_BOARD_OK : BSP_BOARD_ERROR;
}
/** @brief 启用内部HSI 8MHz并切换为系统源，关闭HSE/PLL并建立1ms SysTick。
 * @return 成功BSP_BOARD_OK，HSI就绪、切换或读回失败BSP_BOARD_ERROR，不启动依赖时钟的业务。 */
int BSP_Board_Init(void)
{
    uint32_t polls = BSP_BOARD_WAIT_POLLS; // 保存各阶段独立的有限轮询预算，不依赖尚未启动的SysTick。
    /* 先等待HSI就绪并切换系统源，避免关闭仍在使用的HSE或PLL。 */
    RCC->CR = (RCC->CR & ~RCC_CR_HSITRIM) | RCC_CR_HSION |
        (BSP_BOARD_HSI_DEFAULT_TRIM << RCC_CR_HSITRIM_Pos);
    while ((RCC->CR & RCC_CR_HSIRDY) == 0U && polls != 0U) --polls;
    if ((RCC->CR & RCC_CR_HSIRDY) == 0U) return BSP_BOARD_ERROR;
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) |
        RCC_CFGR_SW_HSI | RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV1 | RCC_CFGR_PPRE2_DIV1;
    polls = BSP_BOARD_WAIT_POLLS;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI && polls != 0U) --polls;
    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) return BSP_BOARD_ERROR;
    RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_HSEON | RCC_CR_CSSON);
    polls = BSP_BOARD_WAIT_POLLS;
    while ((RCC->CR & (RCC_CR_PLLRDY | RCC_CR_HSERDY)) != 0U && polls != 0U) --polls;
    if ((RCC->CR & (RCC_CR_PLLRDY | RCC_CR_HSERDY)) != 0U) return BSP_BOARD_ERROR;
    /* RM0008 p99：确认HSE关闭后才清除旁路；本固件不启用外部晶振。 */
    RCC->CR &= ~RCC_CR_HSEBYP;
    FLASH->ACR &= ~FLASH_ACR_LATENCY; // HSI 8MHz系统时钟使用零等待状态。
    /* CMSIS按SWS更新软件时钟值，随后核对实际源为HSI及既定总线分频。 */
    SystemCoreClockUpdate();
    if (BSP_Board_CheckClock() != BSP_BOARD_OK) return BSP_BOARD_ERROR;
    NVIC_SetPriorityGrouping(BSP_BOARD_PRIORITY_GROUP); // 全部实现位用于抢占优先级，符合Cortex-M3移植契约。
    s_milliseconds = 0U;
    return SysTick_Config(BSP_BOARD_HCLK_HZ / BSP_BOARD_TICK_HZ) == BSP_BOARD_OK ? BSP_BOARD_OK : BSP_BOARD_ERROR;
}
/** @brief 返回基础毫秒计数。
 * @return 32位毫秒计数，允许自然回绕。 */
uint32_t BSP_Time_GetMs(void)
{
    return s_milliseconds;
}
/** @brief 同一个SysTick向基础时基和已启动的FreeRTOS提供1ms节拍。 */
void SysTick_Handler(void)
{
    extern void xPortSysTickHandler(void); // FreeRTOS上游唯一节拍处理入口。
    ++s_milliseconds;
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) xPortSysTickHandler();
}
/** @brief 故障时尽力释放RS485、关闭显示及执行器，保留故障阶段并保持CPU运行以便SWD重连。
 * @param reason 软件检测到的故障阶段。 */
void BSP_Board_Fault(BspBoardFault reason)
{
    __disable_irq();
    g_board_fault = reason;
    BSP_Gpio_OffLocked();
    BSP_Display_Blank();
    if ((RCC->APB2ENR & RCC_APB2ENR_IOPBEN) != 0U) GPIOB->BSRR = GPIO_BSRR_BR2;
    if ((RCC->APB1ENR & RCC_APB1ENR_USART3EN) != 0U) USART3->CR1 = 0U;
    /* RM0008 31.16.1：Sleep调试依赖DBG_SLEEP；故障时不进入睡眠，保留SWD重连所需时钟。 */
    for (;;) __NOP();
}
/* 处理器异常的唯一强入口位于 bsp_backtrace_fault.s，先保存现场再进入诊断。 */
/** @brief 保留不执行应用操作的DebugMonitor入口。 */
void DebugMon_Handler(void) { }
