#ifndef BSP_OLED_H
#define BSP_OLED_H

#include <stdint.h>

#define BSP_OLED_SPI_SCK_PIN          3U
#define BSP_OLED_SPI_MOSI_PIN         5U
#define BSP_OLED_RESET_PIN            6U
#define BSP_OLED_DC_PIN               7U
#define BSP_OLED_CS1_PIN              8U
#define BSP_OLED_RESET_LOW_MS       200U
#define BSP_OLED_RESET_HIGH_MS        1U
#define BSP_OLED_SPI_WAIT_LIMIT  100000U
#define BSP_OLED_SPI_CLOCK_DIVISOR   32U
#define BSP_OLED_SPI_MAX_HZ    10000000UL

/** @brief OLED 板级初始化和传输错误，零表示未发生错误。 */
typedef enum {
    BSP_OLED_ERROR_NONE = 0, /**< 尚未发生错误。 */
    BSP_OLED_ERROR_RESET_LOW, /**< 低电平复位等待失败。 */
    BSP_OLED_ERROR_RESET_HIGH, /**< 释放复位后的等待失败。 */
    BSP_OLED_ERROR_ARGUMENT, /**< 总线未初始化或发送参数无效。 */
    BSP_OLED_ERROR_TXE, /**< 发送缓冲区空标志等待超时。 */
    BSP_OLED_ERROR_RXNE, /**< 接收完成标志等待超时。 */
    BSP_OLED_ERROR_BSY /**< 总线忙标志等待超时。 */
} BspOledError;

extern volatile BspOledError g_bsp_oled_error; // 首次失败原因，供 Keil Watch 查看。
extern volatile uint32_t g_bsp_oled_bytes_sent; // SPI 已完成收发的累计字节数，不代表屏幕已响应。
extern volatile uint32_t g_bsp_oled_debug_cr_before; // 初始化前的调试配置，用于确认跟踪引脚是否曾开启。
extern volatile uint32_t g_bsp_oled_sr_on_error; // 首次错误时的 SPI 状态寄存器快照。

/**
 * @brief 直接配置 SPI1 重映射引脚并复位 OLED。
 * @return 时钟、引脚、SPI 和复位均就绪返回 1，否则返回 0。
 */
uint8_t BSP_Oled_Init(void);

/**
 * @brief 用 SPI1 发送一组命令或数据字节。
 * @param bytes 待发送字节数组。
 * @param length 数组中的字节数，必须大于零。
 * @param is_data 非零表示显示数据，零表示控制命令。
 * @return 全部字节完成发送返回 1，参数无效或硬件超时返回 0。
 */
uint8_t BSP_Oled_Write(const uint8_t *bytes, uint16_t length, uint8_t is_data);

#endif /* BSP_OLED_H */
