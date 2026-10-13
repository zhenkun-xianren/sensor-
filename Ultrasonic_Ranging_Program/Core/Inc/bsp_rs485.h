#ifndef BSP_RS485_H
#define BSP_RS485_H
#include <stdint.h>
#include "bsp_board.h"
/* DS12232 Rev5第41/46/52页：PD8/40=USART3_TX(AF0)，PA10/42=方向GPIO。
 * RM0444 Rev6：USART3为BASIC实例，时钟来自PCLK，使用无FIFO发送路径。 */
#define BSP_RS485_UART_BAUD_RATE       9600UL
#define BSP_RS485_UART_CLOCK_HZ        BSP_BOARD_PCLK1_HZ
#define BSP_RS485_UART_BRR_VALUE       ((BSP_RS485_UART_CLOCK_HZ + BSP_RS485_UART_BAUD_RATE / 2UL) / BSP_RS485_UART_BAUD_RATE)
#define BSP_RS485_TX_TIMEOUT_MS        50U
#define BSP_RS485_TX_PIN               8U
#define BSP_RS485_DIR_PIN              10U
#define BSP_RS485_TX_MASK              (1UL << BSP_RS485_TX_PIN)
#define BSP_RS485_DIR_MASK             (1UL << BSP_RS485_DIR_PIN)
#define BSP_RS485_GPIO_MODE_BITS       2U
#define BSP_RS485_GPIO_FIELD_MASK      3UL
#define BSP_RS485_GPIO_MODE_OUTPUT     1UL
#define BSP_RS485_GPIO_MODE_AF         2UL
#define BSP_RS485_TX_MODE_SHIFT        (BSP_RS485_TX_PIN * BSP_RS485_GPIO_MODE_BITS)
#define BSP_RS485_DIR_MODE_SHIFT       (BSP_RS485_DIR_PIN * BSP_RS485_GPIO_MODE_BITS)
#define BSP_RS485_GPIO_AF_BITS         4U
#define BSP_RS485_GPIO_AF_FIELD_MASK   15UL
#define BSP_RS485_TX_AF_SHIFT          ((BSP_RS485_TX_PIN % 8U) * BSP_RS485_GPIO_AF_BITS)
#define BSP_RS485_TX_AF_VALUE          0UL
#define BSP_RS485_BSRR_RESET_SHIFT     16U
/* UMW 2025-04第6页：DE使能最大90ns；64MHz下至少执行64次NOP，预留>=1us。 */
#define BSP_RS485_ENABLE_GUARD_CYCLES   (BSP_BOARD_HCLK_HZ / 1000000UL)
typedef enum {
    BSP_RS485_OK = 0, // 初始化或本地发送成功。
    BSP_RS485_TIMEOUT, // 等待UART发送状态超时。
    BSP_RS485_INVALID, // 参数错误或板级时钟不符。
    BSP_RS485_NOT_INITIALIZED // UART未初始化或超时后已关闭。
} BspRs485Status;
/**
 * @brief 初始化USART3只发送及PD8/PA10，不改变PD9和其他引脚。
 * @return 成功返回OK，时钟不符返回INVALID，发送使能超时返回TIMEOUT。
 */
BspRs485Status BSP_Rs485_Init(void);
/**
 * @brief 等待TC后释放总线，不接收或等待从站应答。
 * @param data 非空发送序列。
 * @param length 字节数1至255，Modbus接口固定传入8。
 * @return 完整发送返回OK，参数、初始化或超时失败返回对应状态。
 */
BspRs485Status BSP_Rs485_Send(const uint8_t *data, uint8_t length);
#endif
