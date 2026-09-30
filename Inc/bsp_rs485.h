#ifndef BSP_RS485_H
#define BSP_RS485_H

#include <stdint.h>

/**
 * @brief 初始化 PA2、PA3、PA4 和 USART2，配置为 9600 8N1 接收就绪状态。
 * @return 寄存器配置正确返回 1，否则返回 0。
 */
uint8_t BSP_RS485_Init(void);

/**
 * @brief 通过 USART2 发送数据，并在成功或超时后切回 RS485 接收方向。
 * @param data 待发送的字节序列。
 * @param length 待发送的字节数。
 * @param timeout_ms 发送允许的最长时限，单位为毫秒；须在调度器运行后调用。
 * @return 发送成功返回 1；参数无效、尚未初始化或等待超时返回 0。
 */
uint8_t BSP_RS485_Send(const uint8_t *data, uint8_t length,
                       uint32_t timeout_ms);

/**
 * @brief 从 RS485 接收缓冲区读取一个字节。
 * @param byte 用于接收字节的输出指针。
 * @return 读取到字节返回 1；缓冲区为空或参数为空返回 0。
 */
uint8_t BSP_RS485_ReadByte(uint8_t *byte);

/**
 * @brief 处理 USART2 接收中断并将有效接收字节放入环形缓冲区。
 */
void BSP_RS485_IrqHandler(void);

#endif /* BSP_RS485_H */
