#ifndef BSP_BACKTRACE_H
#define BSP_BACKTRACE_H

#include <stdint.h>
#include "bsp_board.h"
#include "bsp_fault.h"

#define BSP_BACKTRACE_LINE_CAPACITY       256U /* 单行含终止符的容量。 */
#define BSP_BACKTRACE_NAME_CAPACITY       16U  /* 与当前任务名最大容量一致。 */
#define BSP_BACKTRACE_SOURCE_CAPACITY     96U  /* 故障源文件名的有界副本。 */
#define BSP_BACKTRACE_CALL_DEPTH          16U
#define BSP_BACKTRACE_DUMP_WORDS          16U
#define BSP_BACKTRACE_OUTPUT_LIMIT        4096U
#define BSP_BACKTRACE_POLL_DIVISOR        100U
#define BSP_BACKTRACE_UART_POLL_LIMIT     (BSP_BOARD_HCLK_HZ / BSP_BACKTRACE_POLL_DIVISOR)
#define BSP_BACKTRACE_SRAM_BEGIN          0x20000000UL /* STM32G071RB：36KB SRAM。 */
#define BSP_BACKTRACE_SRAM_END            0x20009000UL /* 右开边界。 */
#define BSP_BACKTRACE_FLASH_BEGIN         0x08000000UL
#define BSP_BACKTRACE_FLASH_END           0x08020000UL /* STM32G071RB：128KB Flash。 */
#define BSP_BACKTRACE_WORD_BYTES          4U
#define BSP_BACKTRACE_FRAME_WORDS         8U
#define BSP_BACKTRACE_FRAME_BYTES         (BSP_BACKTRACE_FRAME_WORDS * BSP_BACKTRACE_WORD_BYTES)
#define BSP_BACKTRACE_WORD_ALIGN_MASK     (BSP_BACKTRACE_WORD_BYTES - 1U)
#define BSP_BACKTRACE_EXC_HANDLER_MSP     0xFFFFFFF1UL
#define BSP_BACKTRACE_EXC_THREAD_MSP      0xFFFFFFF9UL
#define BSP_BACKTRACE_EXC_THREAD_PSP      0xFFFFFFFDUL
#define BSP_BACKTRACE_EXC_PSP_MASK        (1UL << 2U)
#define BSP_BACKTRACE_XPSR_THUMB_MASK     (1UL << 24U)
#define BSP_BACKTRACE_XPSR_PADDING_MASK   (1UL << 9U)
#define BSP_BACKTRACE_EXCEPTION_NMI      2U
#define BSP_BACKTRACE_CLOCK_MATCH         0

/** @brief 诊断初始化结果，不代表实板验证结果。 */
typedef enum {
    BSP_BACKTRACE_OK = 0, /**< 链接布局有效，诊断库已初始化。 */
    BSP_BACKTRACE_INVALID = -1 /**< 链接栈或代码区配置无效。 */
} BspBacktraceStatus;

/** @brief 本次故障串口输出状态，供调试器观察。 */
typedef enum {
    BSP_BACKTRACE_TX_NOT_STARTED = 0, /**< 尚未接管串口。 */
    BSP_BACKTRACE_TX_ACTIVE, /**< 已接管，尚未等待最终TC。 */
    BSP_BACKTRACE_TX_COMPLETE, /**< 所有已输出字节发送完成。 */
    BSP_BACKTRACE_TX_CLOCK_INVALID, /**< 时钟不符合约定，未发送。 */
    BSP_BACKTRACE_TX_TIMEOUT, /**< 状态轮询耗尽，关闭UART释放方向。 */
    BSP_BACKTRACE_TX_LIMIT, /**< 达到4KB上限，已停止追加。 */
    BSP_BACKTRACE_TX_FORMAT_FAILED, /**< 格式化失败，输出已终止。 */
    BSP_BACKTRACE_TX_REENTERED /**< 诊断重入，禁止再次回溯。 */
} BspBacktraceTxStatus;

/** @brief 原始故障信息与输出结果，保留在RAM中。 */
typedef struct {
    uint32_t exception_number; // IPSR异常号；软件故障为进入故障时的异常号。
    uint32_t exc_return; // 硬件异常的原始EXC_RETURN；软件故障未提供。
    uint32_t entry_msp; // 硬件异常入口MSP；软件故障仅使用entry_sp。
    uint32_t entry_psp; // 硬件异常入口PSP，供核对库读取的PSP。
    uint32_t entry_sp; // 被回溯的原始栈指针，不是诊断栈。
    uint32_t stack_begin; // 经校验的栈低地址。
    uint32_t stack_end; // 经校验的栈右开边界。
    uint32_t saved_regs[BSP_BACKTRACE_FRAME_WORDS]; // R0-R3、R12、LR、PC、xPSR。
    uint32_t bytes_sent; // 已写入TDR的字节数，TC成功前不代表线缆发送完成。
    BspBacktraceTxStatus tx_status; // 本次诊断输出的最终状态。
    uint32_t frame_valid; // 使用具名有效标志表示异常帧是否可读取。
    uint32_t stack_valid; // 使用具名有效标志表示栈边界是否可回溯。
    char task_name[BSP_BACKTRACE_NAME_CAPACITY]; // 诊断时冻结的有界任务名。
} BspBacktraceRecord;

extern volatile BspBacktraceRecord g_bsp_backtrace; // 用户通过调试器查看的完整诊断记录。

/** @brief 初始化链接布局和诊断库，不初始化GPIO或USART。
 * @return BSP_BACKTRACE_OK表示成功，否则布局无效。 */
int BSP_Backtrace_Init(void);
/** @brief 在独立诊断栈上有界格式化并轮询输出一行。
 * @param format 静态格式字符串及其后匹配的可变参数。 */
void BSP_Backtrace_Println(const char *format, ...);
/** @brief 在正常任务切换中发布栈范围和名称，不查询内核、不操作硬件。
 * @param stack_begin 任务栈低地址。
 * @param stack_end 可用栈右开边界，来自pxEndOfStack加一个栈字。
 * @param name 内核提供的任务名，复制长度受限。 */
void BSP_Backtrace_TaskSnapshot(uint32_t stack_begin, uint32_t stack_end, const char *name);
/** @brief 汇编异常入口切栈后的不返回处理函数。
 * @param exc_return 原始EXC_RETURN。
 * @param entry_msp 原始MSP。
 * @param entry_psp 原始PSP。
 * @param exception_number 异常入口的IPSR编号。 */
void BSP_Backtrace_Fault(uint32_t exc_return, uint32_t entry_msp,
                         uint32_t entry_psp, uint32_t exception_number);
/** @brief 软件故障切栈后的不返回处理函数，保留现有故障接口。
 * @param code 原始故障原因。
 * @param file 原始静态源文件名或空指针。
 * @param line 原始代码行号。
 * @param entry_sp 切换诊断栈之前的有效SP。 */
void BSP_Backtrace_Stop(BspFaultCode code, const char *file, uint32_t line, uint32_t entry_sp);
/** @brief 重入时停止超声发射和串口，释放总线后永久停止，不格式化。 */
void BSP_Backtrace_Reentry(void);

#endif
