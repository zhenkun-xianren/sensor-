#include "port_uart.h"
#include "bsp_uart.h"
#include <stddef.h>

static DevUart s_uart; // 保存本工程唯一的原始字节串口对象。

/**
 * @brief 将合法设备的初始化请求交给 BSP。
 * @param self GetUart 返回的唯一设备对象。
 * @return 对象错误返回 ARGUMENT，否则返回板级初始化结果。
 */
static DevUartStatus uart_init(DevUart *self)
{
    if (self != &s_uart) return DEV_UART_ERROR_ARGUMENT;
    return BSP_Uart_Init();
}

/**
 * @brief 通过 BSP 非阻塞读取一个字节，不进行任何协议或文本转换。
 * @param self GetUart 返回的唯一设备对象。
 * @param data 接收读出字节的非空输出指针。
 * @return 参数错误返回 ARGUMENT，否则返回板级读取结果。
 */
static DevUartStatus uart_read_byte(DevUart *self, uint8_t *data)
{
    if (self != &s_uart || data == NULL) return DEV_UART_ERROR_ARGUMENT;
    return BSP_Uart_ReadByte(data);
}

/**
 * @brief 通过 BSP 非阻塞提交一个原始发送字节。
 * @param self GetUart 返回的唯一设备对象。
 * @param data 要原样发送的八位数据。
 * @return 对象错误返回 ARGUMENT，否则返回板级发送队列结果。
 */
static DevUartStatus uart_write_byte(DevUart *self, uint8_t data)
{
    if (self != &s_uart) return DEV_UART_ERROR_ARGUMENT;
    return BSP_Uart_WriteByte(data);
}

/**
 * @brief 绑定原始字节串口的方法并返回静态对象，不操作硬件。
 * @return 返回唯一串口对象的非空指针。
 */
DevUart *GetUart(void)
{
    s_uart.init = uart_init;
    s_uart.readByte = uart_read_byte;
    s_uart.writeByte = uart_write_byte;
    return &s_uart;
}
