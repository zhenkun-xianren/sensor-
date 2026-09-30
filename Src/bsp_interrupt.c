#include "bsp_gpio.h"
#include "bsp_time.h"
#include "stm32f103xb.h"

static volatile uint32_t s_fault_code; // 最近一次处理器异常代码，供 SWD 调试查看。

/**
 * @brief 记录异常，关闭 WARNING 灯并停在安全诊断状态。
 * @param code 与异常入口对应的诊断编号。
 */
static void fault_stop(uint32_t code)
{
    s_fault_code = code;
    BSP_Gpio_IndicateFault();
    __disable_irq();
    for (;;) { }
}

/**
 * @brief 处理不可屏蔽中断。
 */
void NMI_Handler(void)
{
    fault_stop(1U);
}

/**
 * @brief 处理硬故障异常。
 */
void HardFault_Handler(void)
{
    fault_stop(2U);
}

/**
 * @brief 处理内存管理异常。
 */
void MemManage_Handler(void)
{
    fault_stop(3U);
}

/**
 * @brief 处理总线访问异常。
 */
void BusFault_Handler(void)
{
    fault_stop(4U);
}

/**
 * @brief 处理非法指令或状态异常。
 */
void UsageFault_Handler(void)
{
    fault_stop(5U);
}

/**
 * @brief 为板级时基处理每毫秒一次的 SysTick 中断。
 */
void SysTick_Handler(void)
{
    BSP_Time_OnSysTick();
}
