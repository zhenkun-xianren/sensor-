#ifndef BSP_RS485_H
#define BSP_RS485_H

#include <stdint.h>
#include "stm32f0xx.h"
#include "bsp_board.h"
#include "dev_modbus_slave_config.h"

/* STM32F051 USART1使用PCLK，TIM14独占微秒比较；不使用HAL或RTOS。 */
#define BSP_RS485_CLOCK_UNDIVIDED      0UL
#define BSP_RS485_CLOCK_DISABLED       0UL
#define BSP_RS485_UART_READY_MASK      (USART_ISR_TEACK | USART_ISR_REACK)
#define BSP_RS485_UART_CLOCK_HZ        BSP_BOARD_PCLK2_HZ
#define BSP_RS485_UART_BAUD_RATE       DEV_MODBUS_SLAVE_BAUD_RATE
#define BSP_RS485_UART_BRR             ((BSP_RS485_UART_CLOCK_HZ + BSP_RS485_UART_BAUD_RATE / 2UL) / BSP_RS485_UART_BAUD_RATE)
#define BSP_RS485_UART_AF              1UL
#define BSP_RS485_CHARACTER_BITS       10UL /* 库默认8N1，区别于标准11位格式。 */
#define BSP_RS485_TIMER_HZ             1000000UL
#define BSP_RS485_TIMER_MAX            65535UL
#define BSP_RS485_TIMER_PSC            (BSP_BOARD_TIM14_HZ / BSP_RS485_TIMER_HZ - 1UL)
#define BSP_RS485_T15_US               ((3UL * BSP_RS485_CHARACTER_BITS * BSP_RS485_TIMER_HZ + 2UL * BSP_RS485_UART_BAUD_RATE - 1UL) / (2UL * BSP_RS485_UART_BAUD_RATE))
#define BSP_RS485_T35_US               ((7UL * BSP_RS485_CHARACTER_BITS * BSP_RS485_TIMER_HZ + 2UL * BSP_RS485_UART_BAUD_RATE - 1UL) / (2UL * BSP_RS485_UART_BAUD_RATE))
#define BSP_RS485_TX_ENABLE_US         2UL /* ADM3485E手册第6页：最坏使能900ns，保留余量。 */
#define BSP_RS485_TX_MARGIN_MS         20UL
#define BSP_RS485_MILLISECONDS         1000UL
#define BSP_RS485_UART_IRQ_PRIORITY    0U
#define BSP_RS485_TIMER_IRQ_PRIORITY   1U

#if (BSP_RS485_UART_BAUD_RATE > 19200UL) || (BSP_RS485_T35_US > BSP_RS485_TIMER_MAX)
#error This_board_adapter_supports_low_speed_RTU_only
#endif
#if (BSP_BOARD_TIM14_HZ % BSP_RS485_TIMER_HZ) != 0UL
#error TIM14_clock_must_provide_exact_microsecond_ticks
#endif

/** @brief 板级非阻塞传输状态。 */
typedef enum {
    BSP_RS485_OK = 0,              /**< 初始化成功、已借出帧、或已启动发送。 */
    BSP_RS485_BUSY = 1,            /**< 正在传输、保护间隔或总线冲突。 */
    BSP_RS485_NO_FRAME = 2,        /**< 没有可借出的完整接收帧。 */
    BSP_RS485_INVALID = 3,         /**< 参数或板级配置无效。 */
    BSP_RS485_NOT_INITIALIZED = 4, /**< 尚未初始化或故障后不可用。 */
    BSP_RS485_TIMEOUT = 5          /**< 发送超过完整帧期限，已释放总线。 */
} BspRs485Status;

/** @brief 初始化PA9/PA10的USART1、PA11方向和TIM14；先完成BSP_Board_Init。
 * @return 初始化结果。 */
BspRs485Status BSP_Rs485_Init(void);
/** @brief 主循环检查异步发送总期限。
 * @return 正常返回OK，发送或保护间隔返回BUSY，故障返回对应错误。 */
BspRs485Status BSP_Rs485_Poll(void);
/** @brief 借用静态接收缓冲区，期间中断不会覆盖帧内容。
 * @param data 接收只读帧指针的非空输出地址。
 * @param length 接收帧长度的非空输出指针。
 * @return 成功返回OK，无完整帧返回NO_FRAME，参数错误返回INVALID。 */
BspRs485Status BSP_Rs485_AcquireFrame(const uint8_t **data, uint16_t *length);
/** @brief 释放借用帧；若期间出现总线活动，等待新的完整静默间隔。 */
void BSP_Rs485_ReleaseFrame(void);
/** @brief 在完整静默的IDLE状态主动发送，或从HELD状态应答；成功后自动释放接收帧。
 * @param data 异步发送期间必须保持不变的非空字节序列。
 * @param length 发送长度，范围1至256。
 * @return 已提交返回OK，冲突返回BUSY，参数或未初始化返回对应状态。 */
BspRs485Status BSP_Rs485_StartSend(const uint8_t *data, uint16_t length);
/** @brief USART1接收、发送及TC中断衔接。 */
void BSP_Rs485_IrqHandler(void);
/** @brief TIM14管理字节间隔、帧间隔及发送使能建立时间。 */
void BSP_Rs485_TimerIrqHandler(void);

#endif
