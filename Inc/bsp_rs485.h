#ifndef BSP_RS485_H
#define BSP_RS485_H

#include <stdint.h>

/**
 * @brief 初始化 PA2、PA3、PA4 和 USART2，配置为 9600 8N1 接收就绪状态。
 * @return 寄存器配置正确返回 1，否则返回 0。
 */
uint8_t BSP_RS485_Init(void);

#endif /* BSP_RS485_H */
