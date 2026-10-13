#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include "stm32g0xx.h"
#include "bsp_backtrace.h"
#include "bsp_rs485.h"
#include "bsp_ultrasonic_tx.h"
#include "bsp_ultrasonic_range.h"
#include "cm_backtrace.h"

/* 上游已有的任意栈回溯实现尚未列入cm_backtrace.h；仅补声明，不改库源码。 */
/** @brief 在明确的栈边界内提取调用地址。
 * @param buffer 调用地址输出数组。
 * @param size 数组容量，单位为地址项。
 * @param sp 原始栈指针。
 * @param stack_start_addr 栈低地址。
 * @param stack_size 栈大小，单位为字节。
 * @return 实际提取的调用地址数量。 */
extern size_t cm_backtrace_call_stack_any(uint32_t *buffer, size_t size, uint32_t sp,
                                         uint32_t stack_start_addr, uint32_t stack_size);

extern const uint32_t STACK$$Base; // 启动文件STACK区域低地址的链接符号。
extern const uint32_t STACK$$Limit; // 原始主栈的右开边界链接符号。
extern const uint32_t Image$$ER_IROM1$$Base; // 正式固件代码区域低地址。
extern const uint32_t Image$$ER_IROM1$$Limit; // 正式固件代码区域右开边界。

enum {
    BACKTRACE_NOT_ENTERED = 0, // 汇编可直接读写的未进入标志。
    BACKTRACE_ENTERED = 1, // 已进入诊断，禁止重复初始化和快照更新。
    BACKTRACE_METADATA_INVALID = 0, // 元数据无效，不用于解引用。
    BACKTRACE_METADATA_VALID = 1, // 校验有效的完整元数据。
    BACKTRACE_REG_LR = 5, // 硬件异常帧中的LR索引。
    BACKTRACE_REG_PC = 6, // 硬件异常帧中的PC索引。
    BACKTRACE_REG_XPSR = 7 // 硬件异常帧中的xPSR索引。
};

volatile uint32_t g_backtrace_entry_active = BACKTRACE_NOT_ENTERED; // 汇编在切栈前设置的重入保护。
volatile BspBacktraceRecord g_bsp_backtrace; // 本次诊断原始现场及输出状态。
static bool s_ready = false; // CmBacktrace链接布局及初始化是否有效。
static bool s_uart_active = false; // 故障发送是否已接管UART及方向GPIO。
static volatile uint32_t s_snapshot_valid = BACKTRACE_METADATA_INVALID; // 完整任务快照发布标志。
static volatile uint32_t s_task_begin; // 正常任务切换保存的任务栈低地址。
static volatile uint32_t s_task_end; // 正常任务切换保存的任务栈右开边界。
static volatile char s_task_name[BSP_BACKTRACE_NAME_CAPACITY]; // 正常切换复制的任务名。
static char s_line[BSP_BACKTRACE_LINE_CAPACITY]; // 格式化行缓冲，避免大数组占用诊断栈。
static char s_source[BSP_BACKTRACE_SOURCE_CAPACITY]; // 源文件名有界副本，禁止直接扫描未知指针。
static uint32_t s_call_stack[BSP_BACKTRACE_CALL_DEPTH]; // 软件故障调用地址数组。
static uint32_t s_frozen_begin; // 本次故障冻结的栈低地址。
static uint32_t s_frozen_end; // 本次故障冻结的栈右开边界。
static char s_frozen_name[BSP_BACKTRACE_NAME_CAPACITY]; // 本次故障冻结的任务名。

/** @brief 检查完整范围位于指定右开内存区间，避免加法溢出。
 * @param address 待访问地址。
 * @param length 待访问字节数。
 * @param begin 内存区间低地址。
 * @param end 内存区间右开边界。
 * @return 全部字节均可访问时为真。 */
static bool backtrace_range(uint32_t address, uint32_t length, uint32_t begin, uint32_t end)
{
    return address >= begin && address <= end && length <= end - address;
}

/** @brief 检查栈边界对齐及所属SRAM区间。
 * @param begin 栈低地址。
 * @param end 栈右开边界。
 * @return 有效、非空、对齐的SRAM栈区间返回真。 */
static bool backtrace_stack_range(uint32_t begin, uint32_t end)
{
    return begin < end && ((begin | end) & BSP_BACKTRACE_WORD_ALIGN_MASK) == 0U &&
           backtrace_range(begin, end - begin, BSP_BACKTRACE_SRAM_BEGIN, BSP_BACKTRACE_SRAM_END);
}

/** @brief 使用内核正常切换提供的元数据发布快照，不调用任何内核API。
 * @param stack_begin 任务栈低地址。
 * @param stack_end 可用栈右开边界。
 * @param name 内核任务名指针，必须指向完整任务名容量的SRAM。 */
void BSP_Backtrace_TaskSnapshot(uint32_t stack_begin, uint32_t stack_end, const char *name)
{
    uint32_t index; // 有界复制任务名的索引。
    if (g_backtrace_entry_active != BACKTRACE_NOT_ENTERED) return;
    s_snapshot_valid = BACKTRACE_METADATA_INVALID;
    __DMB(); /* NMI中看到无效标志时跳过任务栈，而不是读取半更新数据。 */
    if (!backtrace_stack_range(stack_begin, stack_end) || name == NULL ||
        !backtrace_range((uint32_t)name, BSP_BACKTRACE_NAME_CAPACITY,
                         BSP_BACKTRACE_SRAM_BEGIN, BSP_BACKTRACE_SRAM_END)) return;
    s_task_begin = stack_begin;
    s_task_end = stack_end;
    for (index = 0U; index < BSP_BACKTRACE_NAME_CAPACITY - 1U; ++index) {
        s_task_name[index] = name[index];
        if (name[index] == '\0') break;
    }
    s_task_name[index] = '\0';
    __DMB(); /* 地址和名称全部写完后才发布有效状态。 */
    s_snapshot_valid = BACKTRACE_METADATA_VALID;
}

/** @brief 从正常运行快照冻结任务元数据，不访问TCB或调度器。
 * @return 快照完整且栈范围有效时为真。 */
static bool backtrace_freeze_task(void)
{
    uint32_t index; // 复制完整快照名称的索引。
    if (s_snapshot_valid != BACKTRACE_METADATA_VALID) return false;
    __DMB();
    s_frozen_begin = s_task_begin;
    s_frozen_end = s_task_end;
    for (index = 0U; index < BSP_BACKTRACE_NAME_CAPACITY; ++index) {
        s_frozen_name[index] = s_task_name[index];
    }
    s_frozen_name[BSP_BACKTRACE_NAME_CAPACITY - 1U] = '\0';
    return s_snapshot_valid == BACKTRACE_METADATA_VALID &&
           backtrace_stack_range(s_frozen_begin, s_frozen_end);
}

/** @brief CmBacktrace适配：仅返回故障时冻结的任务栈地址。
 * @return 已校验任务栈低地址，不查询FreeRTOS。 */
uint32_t *vTaskStackAddr(void)
{
    return (uint32_t *)s_frozen_begin;
}

/** @brief CmBacktrace适配：仅返回冻结的任务栈字数。
 * @return 栈字数，单位与上游StackType_t一致。 */
uint32_t vTaskStackSize(void)
{
    return (s_frozen_end - s_frozen_begin) / BSP_BACKTRACE_WORD_BYTES;
}

/** @brief CmBacktrace适配：仅返回有界任务名副本。
 * @return 冻结的任务名，不访问TCB。 */
char *vTaskName(void)
{
    return s_frozen_name;
}

/** @brief 无等待释放PA10并关闭已接管UART，不改变其他引脚。 */
static void backtrace_uart_release(void)
{
    if ((RCC->IOPENR & RCC_IOPENR_GPIOAEN) != 0U) {
        GPIOA->BSRR = BSP_RS485_DIR_MASK << BSP_RS485_BSRR_RESET_SHIFT;
    }
    if (s_uart_active) USART3->CR1 = 0U; /* 停止挂起发送；方向脚已释放。 */
    s_uart_active = false;
    __DSB();
}

/** @brief 有限轮询USART状态，完全不依赖Tick或中断。
 * @param flag 需要置位的USART_ISR标志。
 * @return 标志置位为真，耗尽预算时关闭UART并释放总线返回假。 */
static bool backtrace_uart_wait(uint32_t flag)
{
    uint32_t remaining = BSP_BACKTRACE_UART_POLL_LIMIT; // 有限迭代预算，不表示精确毫秒。
    while (remaining != 0U) {
        if ((USART3->ISR & flag) != 0U) return true;
        --remaining;
        __NOP();
    }
    g_bsp_backtrace.tx_status = BSP_BACKTRACE_TX_TIMEOUT;
    backtrace_uart_release();
    return false;
}

/** @brief 在故障上下文接管USART3的9600/8N1发送，复用现行引脚定义。
 * @return UART和方向建立成功时为真，失败时方向保持释放。 */
static bool backtrace_uart_begin(void)
{
    uint32_t guard; // DE输出建立的有界等待次数。
    g_bsp_backtrace.bytes_sent = 0U;
    g_bsp_backtrace.tx_status = BSP_BACKTRACE_TX_NOT_STARTED;
    backtrace_uart_release();
    if (BSP_Board_CheckClock() != BSP_BACKTRACE_CLOCK_MATCH ||
        (RCC->CR & RCC_CR_PLLRDY) == 0U) {
        g_bsp_backtrace.tx_status = BSP_BACKTRACE_TX_CLOCK_INVALID;
        return false;
    }
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIODEN;
    RCC->APBENR1 |= RCC_APBENR1_USART3EN;
    (void)RCC->IOPENR;
    (void)RCC->APBENR1;
    USART3->CR1 = 0U; /* UE清零取消可能未完成的Modbus帧，不等待普通驱动。 */
    s_uart_active = true;
    GPIOA->BSRR = BSP_RS485_DIR_MASK << BSP_RS485_BSRR_RESET_SHIFT;
    GPIOD->BSRR = BSP_RS485_TX_MASK;
    GPIOA->OTYPER &= ~BSP_RS485_DIR_MASK;
    GPIOA->PUPDR &= ~(BSP_RS485_GPIO_FIELD_MASK << BSP_RS485_DIR_MODE_SHIFT);
    GPIOA->OSPEEDR &= ~(BSP_RS485_GPIO_FIELD_MASK << BSP_RS485_DIR_MODE_SHIFT);
    GPIOA->MODER = (GPIOA->MODER & ~(BSP_RS485_GPIO_FIELD_MASK << BSP_RS485_DIR_MODE_SHIFT)) |
                  (BSP_RS485_GPIO_MODE_OUTPUT << BSP_RS485_DIR_MODE_SHIFT);
    GPIOD->OTYPER &= ~BSP_RS485_TX_MASK;
    GPIOD->PUPDR &= ~(BSP_RS485_GPIO_FIELD_MASK << BSP_RS485_TX_MODE_SHIFT);
    GPIOD->OSPEEDR &= ~(BSP_RS485_GPIO_FIELD_MASK << BSP_RS485_TX_MODE_SHIFT);
    GPIOD->AFR[1] = (GPIOD->AFR[1] & ~(BSP_RS485_GPIO_AF_FIELD_MASK << BSP_RS485_TX_AF_SHIFT)) |
                   (BSP_RS485_TX_AF_VALUE << BSP_RS485_TX_AF_SHIFT);
    GPIOD->MODER = (GPIOD->MODER & ~(BSP_RS485_GPIO_FIELD_MASK << BSP_RS485_TX_MODE_SHIFT)) |
                  (BSP_RS485_GPIO_MODE_AF << BSP_RS485_TX_MODE_SHIFT);
    USART3->CR2 = 0U; /* STOP=00：1停止位。 */
    USART3->CR3 = 0U; /* 无DMA、硬件DE、硬件流控及接收中断。 */
    USART3->BRR = BSP_RS485_UART_BRR_VALUE;
    USART3->ICR = USART_ICR_TCCF; /* ICR写1清零，不读改写。 */
    USART3->CR1 = USART_CR1_UE | USART_CR1_TE; /* 8位、无校验、16倍采样，仅发送。 */
    if (!backtrace_uart_wait(USART_ISR_TEACK)) return false;
    GPIOA->BSRR = BSP_RS485_DIR_MASK;
    __DSB();
    for (guard = 0U; guard < BSP_RS485_ENABLE_GUARD_CYCLES; ++guard) __NOP();
    g_bsp_backtrace.tx_status = BSP_BACKTRACE_TX_ACTIVE;
    return true;
}

/** @brief 有界发送一个字符，耗尽总输出容量后不再追加。
 * @param byte 要发送的字符字节。 */
static void backtrace_uart_byte(uint8_t byte)
{
    if (!s_uart_active || g_bsp_backtrace.tx_status != BSP_BACKTRACE_TX_ACTIVE) return;
    if (g_bsp_backtrace.bytes_sent >= BSP_BACKTRACE_OUTPUT_LIMIT) {
        g_bsp_backtrace.tx_status = BSP_BACKTRACE_TX_LIMIT;
        return;
    }
    if (!backtrace_uart_wait(USART_ISR_TXE_TXFNF)) return;
    USART3->TDR = byte;
    ++g_bsp_backtrace.bytes_sent;
}

/** @brief 仅故障期间格式化一行，所有发送等待均有迭代上限。
 * @param format 静态格式字符串及其后匹配的可变参数。 */
void BSP_Backtrace_Println(const char *format, ...)
{
    va_list arguments; // 当前日志行可变参数的读取状态。
    int written; // vsnprintf所需长度，负数表示格式化失败。
    uint32_t index; // 遍历行缓冲中的实际字符。
    if (!s_uart_active || g_bsp_backtrace.tx_status != BSP_BACKTRACE_TX_ACTIVE || format == NULL) return;
    va_start(arguments, format);
    written = vsnprintf(s_line, sizeof(s_line), format, arguments);
    va_end(arguments);
    if (written < 0) {
        g_bsp_backtrace.tx_status = BSP_BACKTRACE_TX_FORMAT_FAILED;
        backtrace_uart_release();
        return;
    }
    s_line[sizeof(s_line) - 1U] = '\0';
    for (index = 0U; index < sizeof(s_line) - 1U && s_line[index] != '\0'; ++index) {
        backtrace_uart_byte((uint8_t)s_line[index]);
    }
    backtrace_uart_byte((uint8_t)'\r');
    backtrace_uart_byte((uint8_t)'\n');
}

/** @brief 等待最后停止位完成后释放总线，超时同样释放。 */
static void backtrace_uart_finish(void)
{
    if (s_uart_active && g_bsp_backtrace.bytes_sent != 0U && backtrace_uart_wait(USART_ISR_TC)) {
        if (g_bsp_backtrace.tx_status == BSP_BACKTRACE_TX_ACTIVE) {
            g_bsp_backtrace.tx_status = BSP_BACKTRACE_TX_COMPLETE;
        }
    }
    backtrace_uart_release();
}

/** @brief 保存经校验的栈边界和名称，供调试器观察。
 * @param begin 栈低地址。
 * @param end 栈右开边界。
 * @param name 明确有效的静态字符串或冻结名称。 */
static void backtrace_record_stack(uint32_t begin, uint32_t end, const char *name)
{
    uint32_t index; // 复制有界诊断栈名称的索引。
    g_bsp_backtrace.stack_begin = begin;
    g_bsp_backtrace.stack_end = end;
    g_bsp_backtrace.stack_valid = BACKTRACE_METADATA_VALID;
    for (index = 0U; index < BSP_BACKTRACE_NAME_CAPACITY - 1U && name[index] != '\0'; ++index) {
        g_bsp_backtrace.task_name[index] = name[index];
    }
    g_bsp_backtrace.task_name[index] = '\0';
}

/** @brief 完成日志并永久停机，不返回到故障任务或异常现场。 */
static void backtrace_hold(void)
{
    BSP_Backtrace_Println("[CmBacktrace] end; CPU halted until reset.");
    backtrace_uart_finish();
    for (;;) __NOP();
}

/** @brief 初始化工程内CmBacktrace，在板级时钟及APP启动之前调用。
 * @return 代码区和原主栈布局有效时返回BSP_BACKTRACE_OK，否则INVALID。 */
int BSP_Backtrace_Init(void)
{
    uint32_t begin = (uint32_t)&STACK$$Base; // 原主栈低地址。
    uint32_t end = (uint32_t)&STACK$$Limit; // 原主栈右开边界。
    uint32_t code_begin = (uint32_t)&Image$$ER_IROM1$$Base; // 实际代码执行区低地址。
    uint32_t code_end = (uint32_t)&Image$$ER_IROM1$$Limit; // 实际代码执行区右开边界。
    s_ready = false;
    s_snapshot_valid = BACKTRACE_METADATA_INVALID;
    if (!backtrace_stack_range(begin, end) || code_begin >= code_end ||
        !backtrace_range(code_begin, code_end - code_begin,
                         BSP_BACKTRACE_FLASH_BEGIN, BSP_BACKTRACE_FLASH_END)) return BSP_BACKTRACE_INVALID;
    cm_backtrace_init("Ultrasonic_Ranging_Program", "STM32G071RBT6", "CMB-20261012");
    s_ready = true;
    return BSP_BACKTRACE_OK;
}

/** @brief 无日志处理诊断重入，释放RS485且停止超声收发后永久停机。 */
void BSP_Backtrace_Reentry(void)
{
    __disable_irq();
    g_bsp_backtrace.tx_status = BSP_BACKTRACE_TX_REENTERED;
    backtrace_uart_release();
    (void)BSP_UltrasonicTx_Stop();
    (void)BSP_UltrasonicRange_Stop();
    for (;;) __NOP();
}

/** @brief 在独立栈上校验硬件异常帧并调用CmBacktrace，不查询RTOS。
 * @param exc_return 异常入口原始LR。
 * @param entry_msp 异常入口MSP。
 * @param entry_psp 异常入口PSP。
 * @param exception_number 异常入口IPSR编号。 */
void BSP_Backtrace_Fault(uint32_t exc_return, uint32_t entry_msp,
                         uint32_t entry_psp, uint32_t exception_number)
{
    bool uses_psp = (exc_return & BSP_BACKTRACE_EXC_PSP_MASK) != 0U; // 硬件异常帧使用的栈类型。
    bool metadata_ok; // 当前栈元数据是否完整有效。
    bool frame_ok; // 硬件异常帧是否完整位于SRAM且对齐。
    uint32_t frame = uses_psp ? entry_psp : entry_msp; // 原始硬件异常帧地址。
    uint32_t begin = (uint32_t)&STACK$$Base; // 默认使用原始主栈低地址。
    uint32_t end = (uint32_t)&STACK$$Limit; // 默认使用原始主栈右开边界。
    uint32_t frame_bytes = BSP_BACKTRACE_FRAME_BYTES; // 硬件帧及可选对齐填充总字节数。
    uint32_t index; // 复制已经验证的八个硬件保存寄存器。
    __disable_irq();
    (void)BSP_UltrasonicTx_Stop(); /* TIM1独立运行，打印之前立即无等待关闭。 */
    (void)BSP_UltrasonicRange_Stop(); /* 取消接收定时器并恢复钳位，无等待或RTOS调用。 */
    g_bsp_fault.code = exception_number == BSP_BACKTRACE_EXCEPTION_NMI ? BSP_FAULT_NMI : BSP_FAULT_HARD;
    g_bsp_fault.file = NULL;
    g_bsp_fault.line = 0U;
    g_bsp_backtrace.exception_number = exception_number;
    g_bsp_backtrace.exc_return = exc_return;
    g_bsp_backtrace.entry_msp = entry_msp;
    g_bsp_backtrace.entry_psp = entry_psp;
    g_bsp_backtrace.entry_sp = frame;
    g_bsp_backtrace.frame_valid = BACKTRACE_METADATA_INVALID;
    g_bsp_backtrace.stack_valid = BACKTRACE_METADATA_INVALID;
    metadata_ok = uses_psp ? backtrace_freeze_task() : backtrace_stack_range(begin, end);
    if (uses_psp && metadata_ok) {
        begin = s_frozen_begin;
        end = s_frozen_end;
    }
    frame_ok = (exc_return == BSP_BACKTRACE_EXC_HANDLER_MSP ||
                exc_return == BSP_BACKTRACE_EXC_THREAD_MSP ||
                exc_return == BSP_BACKTRACE_EXC_THREAD_PSP) &&
               (frame & BSP_BACKTRACE_WORD_ALIGN_MASK) == 0U &&
               backtrace_range(frame, frame_bytes, BSP_BACKTRACE_SRAM_BEGIN, BSP_BACKTRACE_SRAM_END);
    if (frame_ok) {
        for (index = 0U; index < BSP_BACKTRACE_FRAME_WORDS; ++index) {
            g_bsp_backtrace.saved_regs[index] = ((const uint32_t *)frame)[index];
        }
        frame_ok = (g_bsp_backtrace.saved_regs[BACKTRACE_REG_XPSR] & BSP_BACKTRACE_XPSR_THUMB_MASK) != 0U;
        if ((g_bsp_backtrace.saved_regs[BACKTRACE_REG_XPSR] & BSP_BACKTRACE_XPSR_PADDING_MASK) != 0U) {
            frame_bytes += BSP_BACKTRACE_WORD_BYTES;
        }
        if (frame_ok) g_bsp_backtrace.frame_valid = BACKTRACE_METADATA_VALID;
    }
    metadata_ok = metadata_ok && frame_ok && backtrace_range(frame, frame_bytes, begin, end);
    if (metadata_ok) backtrace_record_stack(begin, end, uses_psp ? s_frozen_name : "MSP");
    (void)backtrace_uart_begin();
    BSP_Backtrace_Println("\r\n[CmBacktrace] exception=%lu EXC_RETURN=%08lx MSP=%08lx PSP=%08lx",
                         (unsigned long)exception_number, (unsigned long)exc_return,
                         (unsigned long)entry_msp, (unsigned long)entry_psp);
    if (frame_ok) {
        BSP_Backtrace_Println("Saved PC=%08lx LR=%08lx xPSR=%08lx",
                             (unsigned long)g_bsp_backtrace.saved_regs[BACKTRACE_REG_PC],
                             (unsigned long)g_bsp_backtrace.saved_regs[BACKTRACE_REG_LR],
                             (unsigned long)g_bsp_backtrace.saved_regs[BACKTRACE_REG_XPSR]);
    }
    if (s_ready && metadata_ok) {
        cm_backtrace_fault(exc_return, entry_msp); /* PSP未被改写；三个vTask适配仅读冻结副本。 */
    } else {
        BSP_Backtrace_Println("Invalid frame/stack metadata or uninitialized library; backtrace skipped.");
    }
    backtrace_hold();
}

/** @brief 从源文件指针有界复制静态字符串，避免格式化器访问未知地址。
 * @param file 原始源文件名；仅接受当前芯片Flash范围中的指针。 */
static void backtrace_copy_source(const char *file)
{
    uint32_t index; // 复制源文件名的有界索引。
    uint32_t address = (uint32_t)file; // 原始字符串地址，仅用于验证。
    s_source[0] = '\0';
    for (index = 0U; index < sizeof(s_source) - 1U; ++index) {
        if (!backtrace_range(address, 1U, BSP_BACKTRACE_FLASH_BEGIN, BSP_BACKTRACE_FLASH_END)) break;
        s_source[index] = *((const char *)address);
        if (s_source[index] == '\0') break;
        ++address;
    }
    s_source[index] = '\0';
}

/** @brief 软件故障在独立诊断栈上按原SP进行明确范围回溯，不伪造硬件异常帧。
 * @param code 原始故障原因。
 * @param file 原始静态源文件名或空指针。
 * @param line 原始代码行号。
 * @param entry_sp 汇编切栈前捕获的当前SP。 */
void BSP_Backtrace_Stop(BspFaultCode code, const char *file, uint32_t line, uint32_t entry_sp)
{
    uint32_t begin = (uint32_t)&STACK$$Base; // 优先检查原主栈范围。
    uint32_t end = (uint32_t)&STACK$$Limit; // 原主栈右开边界。
    uint32_t address; // 软件故障栈转储中的当前地址。
    uint32_t index; // 遍历受限调用地址或转储字。
    size_t depth; // 实际提取的调用地址数量。
    bool metadata_ok; // 是否找到包含原SP的明确有效栈。
    bool uses_task = false; // 原SP是否对应冻结任务快照。
    __disable_irq();
    (void)BSP_UltrasonicTx_Stop();
    (void)BSP_UltrasonicRange_Stop();
    g_bsp_fault.code = (uint32_t)code;
    g_bsp_fault.file = file;
    g_bsp_fault.line = line;
    g_bsp_backtrace.entry_sp = entry_sp;
    g_bsp_backtrace.exception_number = __get_IPSR();
    g_bsp_backtrace.stack_valid = BACKTRACE_METADATA_INVALID;
    g_bsp_backtrace.frame_valid = BACKTRACE_METADATA_INVALID;
    metadata_ok = backtrace_stack_range(begin, end) && backtrace_range(entry_sp, 0U, begin, end);
    if (!metadata_ok && backtrace_freeze_task()) {
        begin = s_frozen_begin;
        end = s_frozen_end;
        metadata_ok = backtrace_range(entry_sp, 0U, begin, end);
        uses_task = metadata_ok;
    }
    metadata_ok = metadata_ok && (entry_sp & BSP_BACKTRACE_WORD_ALIGN_MASK) == 0U;
    if (metadata_ok) backtrace_record_stack(begin, end, uses_task ? s_frozen_name : "MSP");
    (void)backtrace_uart_begin();
    backtrace_copy_source(file);
    BSP_Backtrace_Println("\r\n[CmBacktrace] software fault=%lu source=%s line=%lu SP=%08lx",
                         (unsigned long)code, s_source[0] != '\0' ? s_source : "unavailable",
                         (unsigned long)line, (unsigned long)entry_sp);
    if (s_ready) cm_backtrace_firmware_info();
    if (s_ready && metadata_ok) {
        BSP_Backtrace_Println("Stack=%s range=[%08lx,%08lx)", uses_task ? s_frozen_name : "MSP",
                             (unsigned long)begin, (unsigned long)end);
        depth = cm_backtrace_call_stack_any(s_call_stack, BSP_BACKTRACE_CALL_DEPTH, entry_sp, begin, end - begin);
        for (index = 0U; index < depth; ++index) {
            BSP_Backtrace_Println("call[%lu]=%08lx", (unsigned long)index, (unsigned long)s_call_stack[index]);
        }
        address = entry_sp;
        for (index = 0U; index < BSP_BACKTRACE_DUMP_WORDS &&
             backtrace_range(address, BSP_BACKTRACE_WORD_BYTES, begin, end); ++index) {
            BSP_Backtrace_Println("  addr: %08lx data: %08lx", (unsigned long)address,
                                 (unsigned long)*((const uint32_t *)address));
            address += BSP_BACKTRACE_WORD_BYTES;
        }
    } else {
        BSP_Backtrace_Println("Invalid stack metadata or uninitialized library; backtrace skipped.");
    }
    backtrace_hold();
}
