#ifndef BSP_RS485_H
#define BSP_RS485_H

#include <stdint.h>
#include "dev_modbus_config.h"
#include "bsp_board.h"
#include "stm32f103xb.h"

#define BSP_RS485_UART_BAUD_RATE          9600UL
#define BSP_RS485_UART_CLOCK_HZ           BSP_BOARD_PCLK1_HZ
#define BSP_RS485_UART_BRR_VALUE          ((BSP_RS485_UART_CLOCK_HZ + BSP_RS485_UART_BAUD_RATE / 2UL) / BSP_RS485_UART_BAUD_RATE)
#define BSP_RS485_TX_TIMEOUT_MS           50U
#define BSP_RS485_DIR_PIN                 2U
#define BSP_RS485_DIR_MASK                GPIO_ODR_ODR2
#if DEV_MODBUS_ENABLE_RESPONSE
#define BSP_RS485_IRQ_PRIORITY            6U
#endif

/** @brief STM32F103 参考传输结果，保留旧结果数值并补充未初始化状态。 */
typedef enum {
    BSP_RS485_OK = 0,          /**< 初始化或本地操作成功。 */
    BSP_RS485_TIMEOUT = 1,     /**< 等待发送或接收超过期限。 */
    BSP_RS485_RX_ERROR = 2,    /**< 接收硬件或缓冲区发生错误。 */
    BSP_RS485_INVALID = 3,     /**< 参数或引脚配置无效。 */
    BSP_RS485_NOT_INITIALIZED = 4 /**< 串口未初始化或发送超时后已关闭。 */
} BspRs485Status;

/**
 * @brief 初始化 USART3 和 RS485 方向，按功能宏选择是否启用接收中断。
 * @return 成功返回 OK，引脚配置无效返回 INVALID。
 */
BspRs485Status BSP_Rs485_Init(void);

/**
 * @brief 发送数据并在最后停止位完成后释放总线，发送超时关闭 UART。
 * @param data 非空的待发送字节序列。
 * @param length 待发送字节数，范围为 1 至 255。
 * @return 完整发送返回 OK，参数、未初始化或等待超时返回对应状态。
 */
BspRs485Status BSP_Rs485_Send(const uint8_t *data, uint8_t length);

#if DEV_MODBUS_ENABLE_RESPONSE
/** @brief 丢弃上一事务遗留的接收数据和硬件错误。 */
void BSP_Rs485_ClearRx(void);
/**
 * @brief 在给定时限内从 USART3 环形缓冲区读取一个字节。
 * @param data 保存接收字节的非空输出指针。
 * @param timeoutMs 本次读取允许等待的最长毫秒数。
 * @return 成功返回 OK，参数、未初始化、超时或接收错误返回对应状态。
 */
BspRs485Status BSP_Rs485_ReadByte(uint8_t *data, uint32_t timeoutMs);
/**
 * @brief 读取来源参考工程的 BSP独立毫秒时基，移植时必须替换为平台时基。
 * @return 毫秒计数，允许自然回绕。
 */
uint32_t BSP_Rs485_GetTickMs(void);
/** @brief 处理 USART3 接收中断并把字节写入静态环形缓冲区。 */
void BSP_Rs485_IrqHandler(void);
#endif

#endif /* BSP_RS485_H */
