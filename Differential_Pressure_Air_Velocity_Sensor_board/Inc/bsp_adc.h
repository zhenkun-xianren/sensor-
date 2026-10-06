#ifndef BSP_ADC_H
#define BSP_ADC_H

#include <stdint.h>
#include "bsp_board.h"

/* 正式引脚表V1.0：测量PA6/ADC1_IN6，参考PA7/ADC1_IN7。 */
#define BSP_ADC_SIGNAL_CHANNEL 6UL
#define BSP_ADC_REFERENCE_CHANNEL 7UL
#define BSP_ADC_CLOCK_DIVIDER 2UL
#define BSP_ADC_CLOCK_HZ (BSP_BOARD_PCLK2_HZ / BSP_ADC_CLOCK_DIVIDER)
#define BSP_ADC_CLOCK_MIN_HZ 600000UL /* DS5319 Rev20 PDF第74页。 */
#define BSP_ADC_CLOCK_MAX_HZ 14000000UL
#define BSP_ADC_CALIBRATION_TIMEOUT_MS 20UL
#define BSP_ADC_CONVERSION_TIMEOUT_MS 1UL
#define BSP_ADC_POWER_STABLE_MS 1UL /* 大于DS5319要求的1us以及校准前两个ADC周期。 */
#define BSP_ADC_MILLISECONDS_PER_SECOND 1000UL
#define BSP_ADC_CYCLES_PER_MS (BSP_BOARD_HCLK_HZ / BSP_ADC_MILLISECONDS_PER_SECOND)
#define BSP_ADC_WAIT_POLL_LIMIT 200000UL /* 时基异常时仍能结束轮询。 */
#define BSP_ADC_COUNTER_PROBE_COUNT 16UL
#define BSP_ADC_RAW_MASK 4095UL
#define BSP_ADC_CLEAR_FLAGS 0UL
#define BSP_ADC_RESET_CONFIGURATION 0UL /* 专用ADC1配置寄存器的复位配置值。 */

/** @brief 板级ADC操作结果，Port负责映射为Device状态。 */
typedef enum {
    BSP_ADC_OK = 0, /**< 本次板级操作成功。 */
    BSP_ADC_ARGUMENT_ERROR, /**< 输出指针无效。 */
    BSP_ADC_NOT_READY, /**< ADC未初始化或配置已失效。 */
    BSP_ADC_TIMEOUT, /**< 校准或转换超过等待上限。 */
    BSP_ADC_HARDWARE_ERROR /**< 时钟或精确计时器未满足配置要求。 */
} BspAdcStatus;

/**
 * @brief 配置ADC1与两路模拟输入，建立精确超时时基并执行校准。
 * @return BSP_ADC_OK成功，BSP_ADC_TIMEOUT超时，BSP_ADC_HARDWARE_ERROR配置失败。
 */
BspAdcStatus BSP_Adc_Init(void);
/**
 * @brief 分别执行两个单通道转换，成功后一次性提交两路原始码。
 * @param signal_raw 非空测量通道输出指针，失败时不修改。
 * @param reference_raw 非空参考通道输出指针，必须与signal_raw不同，失败时不修改。
 * @return BSP_ADC_OK成功，BSP_ADC_ARGUMENT_ERROR参数无效，BSP_ADC_NOT_READY未就绪，BSP_ADC_TIMEOUT超时。
 */
BspAdcStatus BSP_Adc_ReadPair(uint16_t *signal_raw, uint16_t *reference_raw);
#endif
