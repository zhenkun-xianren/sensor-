#ifndef BSP_BACKTRACE_H
#define BSP_BACKTRACE_H

#include <stdint.h>

/* STM32F103C8T6 正式目标的片内 SRAM 边界，不把外设或未实现 RAM 当作栈。 */
#define BSP_BACKTRACE_RAM_START 0x20000000UL
#define BSP_BACKTRACE_RAM_END   0x20005000UL
#define BSP_BACKTRACE_FRAME_WORDS 8U
#define BSP_BACKTRACE_FRAME_LR_INDEX 5U
#define BSP_BACKTRACE_FRAME_PC_INDEX 6U
#define BSP_BACKTRACE_FRAME_PSR_INDEX 7U
#define BSP_BACKTRACE_EXC_RETURN_PSP 0xFFFFFFFDUL
#define BSP_BACKTRACE_EXC_RETURN_MSP 0xFFFFFFF9UL
#define BSP_BACKTRACE_EXC_RETURN_HANDLER 0xFFFFFFF1UL
#define BSP_BACKTRACE_UART_POLLS 100000UL
#define BSP_BACKTRACE_LINE_CAPACITY 512U
#define BSP_BACKTRACE_WORD_ALIGNMENT_MASK 3UL

typedef struct {
    uint32_t exception_number; // 保存进入的处理器异常号。
    uint32_t exc_return; // 保存异常入口的原始 LR，区分 MSP 与 PSP。
    uint32_t msp; // 保存切换诊断栈前的 MSP。
    uint32_t psp; // 保存异常入口的 PSP。
    uint32_t cfsr; // 保存本次异常的各类故障状态。
    uint32_t hfsr; // 保存 HardFault 状态。
    uint32_t bfar; // 保存总线故障地址，仅 BFARVALID 时有效。
    uint32_t mmfar; // 保存内存管理故障地址，仅 MMARVALID 时有效。
    uint32_t frame[BSP_BACKTRACE_FRAME_WORDS]; // 保存可读时的标准异常栈帧。
} BspBacktraceSnapshot;

extern volatile BspBacktraceSnapshot g_backtrace_fault; // 保存本次异常摘要供 Keil 查看。
extern volatile uint32_t g_backtrace_entry_active; // 汇编入口的重入保护，复位后清零。
extern volatile uint32_t g_backtrace_uart_timeout_count; // 累计诊断轮询发送超时次数。

/** @brief 初始化诊断库和现有 USART2，成功后允许异常打印。
 * @return 成功返回 BSP_BOARD_OK，否则返回 BSP_BOARD_ERROR。 */
int BSP_Backtrace_Init(void);
/** @brief 将一行诊断格式化为 ASCII 并从 PA2 有界轮询输出，不使用中断或半主机。
 * @param format 与 printf 相同的格式串。
 * @param ... 格式串对应的参数。 */
void BSP_Backtrace_Println(const char *format, ...);
/** @brief 在独立诊断栈上解析异常现场并打印，随后进入原有受控故障保持。
 * @param exc_return 异常入口尚未被 C 调用覆盖的 LR。
 * @param entry_msp 异常入口尚未切换的 MSP。 */
__attribute__((noreturn)) void BSP_Backtrace_Fault(uint32_t exc_return, uint32_t entry_msp);

#endif
