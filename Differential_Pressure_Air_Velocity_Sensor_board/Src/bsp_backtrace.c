#include "bsp_backtrace.h"
#include "bsp_uart.h"
#include "bsp_gpio.h"
#include "bsp_display.h"
#include "cm_backtrace.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ARMCC 链接时禁止引入半主机 I/O；诊断仅格式化字符串并轮询 USART2。 */
#pragma import(__use_no_semihosting)

#define BSP_BACKTRACE_FIRMWARE "Differential_Pressure_Air_Velocity_Sensor_board"
#define BSP_BACKTRACE_HARDWARE "v1.0"
#define BSP_BACKTRACE_SOFTWARE "HSI8-CMB-20261006"
#define BSP_BACKTRACE_UART_ENABLE (USART_CR1_UE | USART_CR1_TE)
#define BSP_BACKTRACE_UART_INTERRUPTS (USART_CR1_TXEIE | USART_CR1_TCIE | USART_CR1_RXNEIE | USART_CR1_IDLEIE | USART_CR1_PEIE)

volatile BspBacktraceSnapshot g_backtrace_fault; // 保存最初异常摘要，不复用后续分析寄存器。
volatile uint32_t g_backtrace_entry_active; // 汇编置位后保持，嵌套异常不再次覆盖诊断栈。
volatile uint32_t g_backtrace_uart_timeout_count; // 保存发送超时统计，便于无输出时排查。
static bool s_ready = false; // 保存诊断库与串口均已初始化的状态。
static bool s_output_failed = false; // 超时后停止本次输出，避免每个字符重复长等待。
static TaskStatus_t s_task_info; // 缓存本次异常任务的公开栈范围及名称信息。
static char s_task_name[configMAX_TASK_NAME_LEN]; // 保存有长度限制且保证结尾的任务名称。
static char s_line[BSP_BACKTRACE_LINE_CAPACITY]; // 保存单行格式化内容，不占用故障任务栈。

/** @brief 提供 ARMCC 无半主机运行库的退出入口，固件退出时受控保持。
 * @param return_code 运行库传入的退出状态；本固件不向宿主机返回。 */
void _sys_exit(int return_code)
{
    (void)return_code;
    BSP_Board_Fault(BSP_FAULT_APP);
    for (;;) __NOP();
}

/** @brief 核对整个内存范围位于正式芯片 SRAM 内且起点为字对齐。
 * @param address 待核对地址。
 * @param length 待访问字节数。
 * @return 范围有效返回 true，否则返回 false。 */
static bool backtrace_ram_range(uint32_t address, uint32_t length)
{
    return address >= BSP_BACKTRACE_RAM_START && address < BSP_BACKTRACE_RAM_END &&
        (address & BSP_BACKTRACE_WORD_ALIGNMENT_MASK) == 0U &&
        length <= BSP_BACKTRACE_RAM_END - address;
}

/** @brief 有界等待串口状态，不依赖 SysTick、调度器或发送中断。
 * @param flag USART_SR 中需要等待的标志。
 * @return 就绪返回 true，超时返回 false 并停止本次诊断输出。 */
static bool backtrace_uart_wait(uint32_t flag)
{
    uint32_t polls = BSP_BACKTRACE_UART_POLLS; // 保存独立的有限轮询预算，不作为精确毫秒时长。
    while ((USART2->SR & flag) == 0U && polls != 0U) --polls;
    if ((USART2->SR & flag) != 0U) return true;
    ++g_backtrace_uart_timeout_count;
    s_output_failed = true;
    return false;
}

/** @brief 诊断专用轮询发送一个字节，异常时不调用正常环形发送队列。
 * @param byte 要写入数据寄存器的 ASCII 字节。 */
static void backtrace_uart_byte(uint8_t byte)
{
    if (!s_output_failed && backtrace_uart_wait(USART_SR_TXE)) USART2->DR = byte;
}

/** @brief 将诊断格式化为一行并直接发送，失败或未准备时不访问串口。
 * @param format printf 格式字符串。
 * @param ... 对应格式参数。 */
void BSP_Backtrace_Println(const char *format, ...)
{
    va_list arguments; // 保存诊断格式参数，不使用堆分配。
    int length; // 保存格式化结果，负值表示格式化失败。
    size_t index; // 遍历本行实际可发送的字符。
    if (!s_ready || s_output_failed || format == NULL) return;
    va_start(arguments, format);
    length = vsnprintf(s_line, sizeof(s_line), format, arguments);
    va_end(arguments);
    if (length < 0) return;
    for (index = 0U; index < sizeof(s_line) - 1U && s_line[index] != '\0'; ++index)
        backtrace_uart_byte((uint8_t)s_line[index]);
    backtrace_uart_byte((uint8_t)'\r');
    backtrace_uart_byte((uint8_t)'\n');
}

/** @brief 获取当前任务的公开栈元数据，不遍历内核链表或扫描栈水位。
 * @return 当前任务控制块和栈范围有效返回 true，否则返回 false。 */
static bool backtrace_task_snapshot(void)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle(); // 保存暂停调度后不再切换的当前任务句柄。
    uint32_t start; // 保存栈起点地址。
    uint32_t last; // 保存栈中最后一个字的地址。
    size_t index; // 有界复制任务名称，避免损坏名称失去字符串结尾。
    if (!backtrace_ram_range((uint32_t)task, sizeof(StaticTask_t))) return false;
    /* eRunning 不查询任务链表，pdFALSE 不扫描栈；此单核工程当前任务直接走运行分支。 */
    vTaskGetInfo(task, &s_task_info, pdFALSE, eRunning);
    start = (uint32_t)s_task_info.pxStackBase;
    last = (uint32_t)s_task_info.pxEndOfStack;
    if (last < start || !backtrace_ram_range(start, sizeof(StackType_t)) ||
        !backtrace_ram_range(last, sizeof(StackType_t))) return false;
    /* pcTaskName 位于已验证的 TCB 内；限制读取长度并显式终止。 */
    for (index = 0U; index < sizeof(s_task_name) - 1U; ++index) {
        s_task_name[index] = s_task_info.pcTaskName[index];
        if (s_task_name[index] == '\0') break;
    }
    s_task_name[sizeof(s_task_name) - 1U] = '\0';
    return true;
}

/** @brief 为上游 CmBacktrace 提供已验证的当前任务栈起址，不访问内核私有结构。
 * @return 本次故障任务栈的最低地址。 */
uint32_t *vTaskStackAddr(void)
{
    return (uint32_t *)s_task_info.pxStackBase;
}

/** @brief 为上游 CmBacktrace 提供完整栈容量，包含最高地址的一个字。
 * @return 当前任务栈容量，单位为 StackType_t 字。 */
uint32_t vTaskStackSize(void)
{
    return (uint32_t)(s_task_info.pxEndOfStack - s_task_info.pxStackBase) + 1U;
}

/** @brief 为上游 CmBacktrace 提供有界复制的任务名称。
 * @return 本次异常任务名称字符串。 */
char *vTaskName(void)
{
    return s_task_name;
}

/** @brief 在应用启动之前配置现有 UART 与诊断库，正常启动不输出额外字节。
 * @return 成功 BSP_BOARD_OK，串口初始化失败 BSP_BOARD_ERROR。 */
int BSP_Backtrace_Init(void)
{
    if (BSP_Uart_Init() != DEV_UART_OK) return BSP_BOARD_ERROR;
    s_ready = true;
    cm_backtrace_init(BSP_BACKTRACE_FIRMWARE, BSP_BACKTRACE_HARDWARE, BSP_BACKTRACE_SOFTWARE);
    return BSP_BOARD_OK;
}

/** @brief 解析独立诊断栈上的异常入口，保存寄存器并输出回溯后保持故障状态。
 * @param exc_return 入口原始 LR。
 * @param entry_msp 入口原始 MSP。 */
__attribute__((noreturn)) void BSP_Backtrace_Fault(uint32_t exc_return, uint32_t entry_msp)
{
    uint32_t frame_address; // 保存按原始 EXC_RETURN 选出的标准栈帧地址。
    uint32_t index; // 复制八个自动保存的异常现场寄存器。
    bool uses_psp = exc_return == BSP_BACKTRACE_EXC_RETURN_PSP; // 区分线程任务栈与主栈异常。
    bool valid_return = uses_psp || exc_return == BSP_BACKTRACE_EXC_RETURN_MSP ||
        exc_return == BSP_BACKTRACE_EXC_RETURN_HANDLER; // 只接受 Cortex-M3 支持的异常返回编码。
    __disable_irq();
    g_board_fault = BSP_FAULT_EXCEPTION;
    g_backtrace_fault.exception_number = __get_IPSR();
    g_backtrace_fault.exc_return = exc_return;
    g_backtrace_fault.msp = entry_msp;
    g_backtrace_fault.psp = __get_PSP();
    g_backtrace_fault.cfsr = SCB->CFSR;
    g_backtrace_fault.hfsr = SCB->HFSR;
    g_backtrace_fault.bfar = SCB->BFAR;
    g_backtrace_fault.mmfar = SCB->MMFAR;
    frame_address = uses_psp ? g_backtrace_fault.psp : entry_msp;
    /* 停止业务输出后再打印，避免 9600 波特率的诊断期间执行器或 RS485 保持活动。 */
    BSP_Gpio_OffLocked();
    BSP_Display_Blank();
    if ((RCC->APB2ENR & RCC_APB2ENR_IOPBEN) != 0U) GPIOB->BSRR = GPIO_BSRR_BR2;
    if ((RCC->APB1ENR & RCC_APB1ENR_USART3EN) != 0U) USART3->CR1 = BSP_UART_DISABLED;
    if (!s_ready || (RCC->APB1ENR & RCC_APB1ENR_USART2EN) == 0U ||
        (USART2->CR1 & BSP_BACKTRACE_UART_ENABLE) != BSP_BACKTRACE_UART_ENABLE)
        BSP_Board_Fault(BSP_FAULT_EXCEPTION);
    NVIC_DisableIRQ(USART2_IRQn);
    USART2->CR1 &= ~(BSP_BACKTRACE_UART_INTERRUPTS | USART_CR1_RE);
    USART2->CR3 &= ~(USART_CR3_DMAT | USART_CR3_DMAR | USART_CR3_EIE);
    (void)backtrace_uart_wait(USART_SR_TC); // 等待已装入硬件的回传字节结束，不排空软件队列。
    BSP_Backtrace_Println("\r\n[CmBacktrace] exception=%lu EXC_RETURN=%08lx MSP=%08lx PSP=%08lx",
        (unsigned long)g_backtrace_fault.exception_number, (unsigned long)exc_return,
        (unsigned long)entry_msp, (unsigned long)g_backtrace_fault.psp);
    BSP_Backtrace_Println("CFSR=%08lx HFSR=%08lx BFAR=%08lx MMFAR=%08lx",
        (unsigned long)g_backtrace_fault.cfsr, (unsigned long)g_backtrace_fault.hfsr,
        (unsigned long)g_backtrace_fault.bfar, (unsigned long)g_backtrace_fault.mmfar);
    if (!valid_return || !backtrace_ram_range(frame_address, sizeof(g_backtrace_fault.frame)) ||
        (g_backtrace_fault.cfsr & (SCB_CFSR_MSTKERR_Msk | SCB_CFSR_STKERR_Msk)) != 0U) {
        BSP_Backtrace_Println("Invalid/incomplete exception frame; backtrace skipped.");
    } else {
        for (index = 0U; index < BSP_BACKTRACE_FRAME_WORDS; ++index)
            g_backtrace_fault.frame[index] = ((const uint32_t *)frame_address)[index];
        BSP_Backtrace_Println("Saved PC=%08lx LR=%08lx xPSR=%08lx",
            (unsigned long)g_backtrace_fault.frame[BSP_BACKTRACE_FRAME_PC_INDEX],
            (unsigned long)g_backtrace_fault.frame[BSP_BACKTRACE_FRAME_LR_INDEX],
            (unsigned long)g_backtrace_fault.frame[BSP_BACKTRACE_FRAME_PSR_INDEX]);
        if (uses_psp && !backtrace_task_snapshot())
            BSP_Backtrace_Println("Invalid task/stack metadata; backtrace skipped.");
        else cm_backtrace_fault(exc_return, entry_msp);
    }
    BSP_Backtrace_Println("[CmBacktrace] end; CPU halted in fault loop.");
    if (!s_output_failed) (void)backtrace_uart_wait(USART_SR_TC);
    BSP_Board_Fault(BSP_FAULT_EXCEPTION);
    for (;;) __NOP(); // 明确保证汇编尾跳转永不返回，故障保持函数意外返回时仍停止执行。
}
