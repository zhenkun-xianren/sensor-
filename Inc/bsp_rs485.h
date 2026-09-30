#ifndef BSP_RS485_H
#define BSP_RS485_H

#include <stdint.h>

/**
 * @brief 初始化 PA2、PA3、PA4 和 USART2，配置为 9600 8N1 接收就绪状态。
 * @return 寄存器配置正确返回 1，否则返回 0。
 */
uint8_t BSP_RS485_Init(void);

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
