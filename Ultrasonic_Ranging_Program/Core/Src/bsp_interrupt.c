#include "stm32g0xx.h"
#include "bsp_time.h"
#include "bsp_rtos.h"

/* HardFault/NMI由bsp_backtrace_fault.s提供，保留原始现场后切到独立诊断栈。 */

/** @brief 保留空SVC接口；当前RVDS/ARM_CM0移植层直接恢复首任务，不使用SVC。 */
void SVC_Handler(void)
{
}

/** @brief 每毫秒先推进BSP计时，再由RTOS衔接判断是否需要推进内核Tick。 */
void SysTick_Handler(void)
{
    BSP_Time_Tick();
    BSP_RtosTick();
}
