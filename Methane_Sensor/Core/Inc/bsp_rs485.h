#ifndef BSP_RS485_H
#define BSP_RS485_H

#include <stdint.h>
#include "bsp_board.h"
#include "bsp_gpio.h"

#define BSP_RS485_UART_BAUD_RATE          9600UL
#define BSP_RS485_UART_CLOCK_HZ           BSP_BOARD_PCLK1_HZ
#define BSP_RS485_UART_BRR_VALUE          (BSP_RS485_UART_CLOCK_HZ / BSP_RS485_UART_BAUD_RATE)
#define BSP_RS485_IRQ_PRIORITY            6U
#define BSP_RS485_TX_TIMEOUT_MS           50U
#define BSP_RS485_DIR_PIN                 BSP_GPIO_PA_RS485_DIRECTION_PIN
#define BSP_RS485_DIR_MASK                BSP_GPIO_PA_RS485_DIRECTION_MASK

typedef enum {
    BSP_RS485_OK = 0,
    BSP_RS485_TIMEOUT,
    BSP_RS485_RX_ERROR,
    BSP_RS485_INVALID
} BspRs485Status;

/**
 * @brief 初始化 USART2 和 ADM3485 的 RS485 收发控制。
 * @return 初始化成功返回 BSP_RS485_OK；引脚配置无效时返回 BSP_RS485_INVALID。
 */
BspRs485Status BSP_Rs485_Init(void);

/**
 * @brief 通过 USART2 发送数据并在发送完成后切回接收方向。
 * @param data 待发送的字节序列。
 * @param length 待发送的字节数。
 * @return 发送成功返回 BSP_RS485_OK；参数错误或硬件等待超时返回对应状态。
 */
BspRs485Status BSP_Rs485_Send(const uint8_t *data, uint8_t length);

/**
 * @brief 丢弃上一事务遗留的接收数据和硬件接收错误。
 */
void BSP_Rs485_ClearRx(void);

/**
 * @brief 在给定时限内从 USART2 接收环形缓冲区读取一个字节。
 * @param data 用于保存接收字节的输出指针。
 * @param timeoutMs 本次读取允许等待的最长时间，单位为毫秒。
 * @return 读取成功返回 BSP_RS485_OK；等待超时或接收错误时返回对应状态。
 */
BspRs485Status BSP_Rs485_ReadByte(uint8_t *data, uint32_t timeoutMs);

/**
 * @brief 读取 HAL 系统毫秒时基以供协议层计算整体应答期限。
 * @return 自 HAL 初始化以来经过的毫秒计数，允许自然回绕。
 */
uint32_t BSP_Rs485_GetTickMs(void);

/**
 * @brief 处理 USART2 接收中断并把接收字节写入静态环形缓冲区。
 */
void BSP_Rs485_IrqHandler(void);

#endif /* BSP_RS485_H */
