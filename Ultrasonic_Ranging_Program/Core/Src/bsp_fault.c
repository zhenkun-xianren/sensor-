#include "bsp_fault.h"

volatile BspFaultRecord g_bsp_fault; // 全局故障记录，供用户通过调试器观察。

/* BSP_Fault_Stop由bsp_backtrace_fault.s唯一实现：先捕获原SP并切栈，再记录/输出。 */

/** @brief 保留通用错误兼容接口，记录板级故障后永久停止，不依赖生成头文件。 */
void Error_Handler(void)
{
    BSP_Fault_Stop(BSP_FAULT_BOARD_INIT, __FILE__, (uint32_t)__LINE__);
}

#if defined(USE_FULL_ASSERT)
/**
 * @brief 保留可选完整断言兼容接口，直接记录位置并停止，不依赖HAL。
 * @param file 断言所在的静态源文件名。
 * @param line 断言所在源代码行号。
 */
void assert_failed(uint8_t *file, uint32_t line)
{
    BSP_Fault_Stop(BSP_FAULT_ASSERT, (const char *)file, line);
}
#endif
