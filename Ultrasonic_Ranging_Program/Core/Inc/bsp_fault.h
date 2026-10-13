#ifndef BSP_FAULT_H
#define BSP_FAULT_H
#include <stdint.h>

/** @brief 框架启动及 RTOS 故障来源，仅用于诊断。 */
typedef enum {
    BSP_FAULT_NONE = 0, /**< 尚未记录故障。 */
    BSP_FAULT_CLOCK = 1, /**< 时钟核对失败。 */
    BSP_FAULT_APP_START = 2, /**< 应用模块启动失败。 */
    BSP_FAULT_SCHEDULER = 3, /**< 调度器异常返回。 */
    BSP_FAULT_ASSERT = 4, /**< RTOS或兼容完整断言失败。 */
    BSP_FAULT_STACK = 5, /**< 任务栈溢出。 */
    BSP_FAULT_ALLOC = 6, /**< RTOS 内存分配失败。 */
    BSP_FAULT_BOARD_INIT = 7, /**< SWD配置、独立基础计时或既有板级外设初始化失败。 */
    BSP_FAULT_HARD = 8, /**< CPU硬故障，原始异常记录见g_bsp_backtrace。 */
    BSP_FAULT_NMI = 9, /**< 不可屏蔽异常，原始异常记录见g_bsp_backtrace。 */
    BSP_FAULT_BACKTRACE_INIT = 10 /**< 诊断链接布局无效，阻止继续启动。 */
} BspFaultCode;

/** @brief 进入受控停止状态前保存的信息，供调试器查看。 */
typedef struct {
    uint32_t code; // BspFaultCode 故障原因，0 表示未记录。
    uint32_t line; // 触发故障的源代码行，0 表示未提供。
    const char *file; // 触发故障的静态源文件名，不指向临时缓冲区。
} BspFaultRecord;

extern volatile BspFaultRecord g_bsp_fault; // 保存最近一次框架故障信息。

/**
 * @brief 汇编捕获原SP并切换诊断栈，关闭连续发射、记录并输出后永久停止。
 * @param code 有效的 BspFaultCode 故障原因。
 * @param file 静态源文件名或空指针。
 * @param line 源代码行号；未提供时为 0。
 */
void BSP_Fault_Stop(BspFaultCode code, const char *file, uint32_t line);

#endif
