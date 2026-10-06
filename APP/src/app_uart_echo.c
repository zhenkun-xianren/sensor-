#include "app_uart_echo.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <stddef.h>

#define APP_UART_ECHO_STACK_WORDS 256U /* 静态任务栈容量，单位为 StackType_t 字。 */
#define APP_UART_ECHO_PRIORITY 1U /* 低于显示服务、测量和红外任务。 */
#define APP_UART_ECHO_BATCH_LIMIT 64U /* 每轮最多提交的字节数，限制单轮工作量。 */
#define APP_UART_ECHO_POLL_MS 1U /* 相邻处理轮次间的阻塞时间，匹配现有 1ms Tick。 */

static DevUart *s_uart; // 保存唯一串口对象，运行期仅本模块任务调用。
static StaticTask_t s_echo_control; // 保存静态回传任务控制块。
static StackType_t s_echo_stack[APP_UART_ECHO_STACK_WORDS]; // 保存静态回传任务栈。
static TaskHandle_t s_echo_task; // 保存已创建任务句柄，避免重复启动。
volatile DevUartStatus g_uart_echo_init_status = DEV_UART_NOT_INITIALIZED; // 保存最近串口初始化结果。
volatile DevUartStatus g_uart_echo_read_status = DEV_UART_NOT_INITIALIZED; // 保存最近接收读取状态。
volatile DevUartStatus g_uart_echo_write_status = DEV_UART_NOT_INITIALIZED; // 保存最近发送提交状态。
volatile uint32_t g_uart_echo_count; // 累计本地入队回传字节数，自然回绕。
volatile uint32_t g_uart_echo_busy_count; // 累计队列满时保留字节的次数，自然回绕。
volatile uint32_t g_uart_echo_error_count; // 累计任务观察到的错误通知，自然回绕。

/**
 * @brief 原样转交接收字节；发送满时保留当前字节，下一轮先重试再读取新数据。
 * @param argument 未使用的任务参数。
 */
static void uart_echo_task(void *argument)
{
    bool pending = false; // 标记当前字节已接收但尚未进入发送队列。
    uint8_t byte = 0; // 保存待回传字节，支持零字节及全部八位取值。
    uint32_t submitted; // 统计本轮成功提交的回传字节数。
    (void)argument;
    for (;;) {
        submitted = 0U;
        while (submitted < APP_UART_ECHO_BATCH_LIMIT) {
            if (!pending) {
                g_uart_echo_read_status = s_uart->readByte(s_uart, &byte);
                if (g_uart_echo_read_status == DEV_UART_EMPTY) break;
                if (g_uart_echo_read_status != DEV_UART_OK) {
                    ++g_uart_echo_error_count;
                    break; // 接收错误通知不提供新字节，下一轮继续读取已有有效队列。
                }
                pending = true;
            }
            g_uart_echo_write_status = s_uart->writeByte(s_uart, byte);
            if (g_uart_echo_write_status == DEV_UART_BUSY) {
                ++g_uart_echo_busy_count;
                break;
            }
            if (g_uart_echo_write_status != DEV_UART_OK) {
                ++g_uart_echo_error_count;
                break; // 未成功提交时保留当前字节，不读取新字节改变顺序。
            }
            pending = false;
            ++g_uart_echo_count;
            ++submitted;
        }
        vTaskDelay(pdMS_TO_TICKS(APP_UART_ECHO_POLL_MS)); // 阻塞让出 CPU，不在空队列或满队列上忙等。
    }
}

/**
 * @brief 获取 Device 对象，检查方法并初始化串口，不改变其他模块业务。
 * @return 初始化成功返回 APP_OK，否则返回 APP_ERROR_UART。
 */
AppStatus App_UartEcho_Init(void)
{
    if (s_echo_task != NULL) return APP_ERROR_UART;
    s_uart = GetUart();
    if (s_uart == NULL || s_uart->init == NULL || s_uart->readByte == NULL ||
        s_uart->writeByte == NULL) return APP_ERROR_UART;
    g_uart_echo_init_status = s_uart->init(s_uart);
    return g_uart_echo_init_status == DEV_UART_OK ? APP_OK : APP_ERROR_UART;
}

/**
 * @brief 创建一次静态回传任务；重复启动返回已有任务的成功状态。
 * @return 返回 APP_OK、APP_ERROR_UART 或 APP_ERROR_TASK。
 */
AppStatus App_UartEcho_Start(void)
{
    if (s_uart == NULL || g_uart_echo_init_status != DEV_UART_OK) return APP_ERROR_UART;
    if (s_echo_task != NULL) return APP_OK;
    s_echo_task = xTaskCreateStatic(uart_echo_task, "uart_echo",
        APP_UART_ECHO_STACK_WORDS, NULL, APP_UART_ECHO_PRIORITY,
        s_echo_stack, &s_echo_control);
    return s_echo_task != NULL ? APP_OK : APP_ERROR_TASK;
}
