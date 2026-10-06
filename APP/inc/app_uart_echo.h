#ifndef APP_UART_ECHO_H
#define APP_UART_ECHO_H

#include "app_main.h"
#include "dev_uart.h"

extern volatile DevUartStatus g_uart_echo_init_status; // 保存串口最近初始化结果。
extern volatile DevUartStatus g_uart_echo_read_status; // 保存最近接收队列读取结果。
extern volatile DevUartStatus g_uart_echo_write_status; // 保存最近发送队列提交结果。
extern volatile uint32_t g_uart_echo_count; // 累计成功提交回传的字节数，不表示线端发送完成。
extern volatile uint32_t g_uart_echo_busy_count; // 累计发送队列满时的重试次数。
extern volatile uint32_t g_uart_echo_error_count; // 累计任务观察到的收发错误通知。

/**
 * @brief 初始化原始字节串口并检查回传需要的全部 Device 方法。
 * @return 成功返回 APP_OK，设备不可用或初始化失败返回 APP_ERROR_UART。
 */
AppStatus App_UartEcho_Init(void);
/**
 * @brief 创建唯一静态回传任务，运行期不分配堆内存。
 * @return 成功返回 APP_OK，设备未就绪返回 APP_ERROR_UART，任务创建失败返回 APP_ERROR_TASK。
 */
AppStatus App_UartEcho_Start(void);

#endif /* APP_UART_ECHO_H */
