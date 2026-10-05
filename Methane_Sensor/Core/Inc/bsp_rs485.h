#ifndef BSP_RS485_H
#define BSP_RS485_H
#include <stdint.h>
#include "bsp_board.h"

#define BSP_RS485_UART_BAUD_RATE       9600UL
#define BSP_RS485_UART_CLOCK_HZ        BSP_BOARD_PCLK2_HZ
#define BSP_RS485_UART_BRR_VALUE       ((BSP_RS485_UART_CLOCK_HZ + BSP_RS485_UART_BAUD_RATE / 2UL) / BSP_RS485_UART_BAUD_RATE)
#define BSP_RS485_IRQ_PRIORITY         6U
#define BSP_RS485_TX_TIMEOUT_MS        50U
#define BSP_RS485_TX_CAPACITY          8U
#define BSP_RS485_TX_PIN               9U
#define BSP_RS485_RX_PIN               10U
#define BSP_RS485_DIR_PIN              8U
#define BSP_RS485_DIR_MASK             (1UL << BSP_RS485_DIR_PIN)
#define BSP_RS485_RX_MASK              (1UL << BSP_RS485_RX_PIN)
#define BSP_RS485_GPIO_FIELD_BITS      4U
#define BSP_RS485_GPIO_FIELD_MASK      0xFUL
#define BSP_RS485_GPIO_CRH_FIRST_PIN   8U
#define BSP_RS485_GPIO_AF_PP_2MHZ       0xAUL /* RM0008 CNF=10、MODE=10。 */
#define BSP_RS485_GPIO_OUT_PP_2MHZ      0x2UL /* RM0008 CNF=00、MODE=10。 */
#define BSP_RS485_GPIO_INPUT_PULL       0x8UL /* RM0008 CNF=10、MODE=00，ODR=1 为上拉。 */
#define BSP_RS485_BSRR_RESET_SHIFT     16U
#define BSP_RS485_CHARACTER_BITS       10UL /* 用户指定 8N1：1+8+1。 */
#define BSP_RS485_CHAR_CYCLES          ((BSP_BOARD_HCLK_HZ * BSP_RS485_CHARACTER_BITS + BSP_RS485_UART_BAUD_RATE - 1UL) / BSP_RS485_UART_BAUD_RATE)
#define BSP_RS485_T1_5_CYCLES          ((BSP_RS485_CHAR_CYCLES * 3UL + 1UL) / 2UL)
#define BSP_RS485_T3_5_CYCLES          ((BSP_RS485_CHAR_CYCLES * 7UL + 1UL) / 2UL)
#define BSP_RS485_FRAME_END_CYCLES     (BSP_RS485_T3_5_CYCLES + BSP_RS485_CHAR_CYCLES)
#define BSP_RS485_ENABLE_SETUP_US      1UL /* UMW 第6页：普通使能最大90ns，关断唤醒最大900ns。 */
#define BSP_RS485_SETUP_CYCLES         ((BSP_BOARD_HCLK_HZ / 1000000UL) * BSP_RS485_ENABLE_SETUP_US)
#define BSP_RS485_SETUP_GUARD          (BSP_RS485_SETUP_CYCLES * 8UL)

/** @brief 板级有界收发操作的结果。 */
typedef enum {
    BSP_RS485_OK = 0,
    BSP_RS485_TIMEOUT,
    BSP_RS485_RX_ERROR,
    BSP_RS485_INVALID,
    BSP_RS485_FRAME_ERROR
} BspRs485Status;

/**
 * @brief 初始化 USART1、PA9/PA10 与 PA8，默认接收方向。
 * @return 成功返回 OK，时钟、时基或上下文不满足返回 INVALID。
 */
BspRs485Status BSP_Rs485_Init(void);
/**
 * @brief 等待总线静默后，中断发送并等 TC 切回接收，任务等待会阻塞让出 CPU。
 * @param data 待发送字节。
 * @param length 长度，范围 1 至 BSP_RS485_TX_CAPACITY。
 * @return 成功返回 OK；超时返回 TIMEOUT；参数或上下文错误返回 INVALID。
 */
BspRs485Status BSP_Rs485_Send(const uint8_t *data, uint8_t length);
/**
 * @brief 在整体期限内接收有 RTU 静默边界的帧，检查字符间隔。
 * @param data 接收缓冲区。
 * @param capacity 缓冲区容量，至少一字节。
 * @param length 输出实际接收的字节数。
 * @param timeoutMs 从上一帧发送完成算起的整体应答期限，单位毫秒。
 * @return 返回 OK、TIMEOUT、RX_ERROR、FRAME_ERROR 或 INVALID。
 */
BspRs485Status BSP_Rs485_ReadFrame(uint8_t *data, uint8_t capacity,
    uint8_t *length, uint32_t timeoutMs);
/**
 * @brief 处理 USART1 接收、发送空寄存器及最终发送完成中断。
 */
void BSP_Rs485_IrqHandler(void);
#endif

