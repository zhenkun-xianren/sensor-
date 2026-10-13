#ifndef BSP_DISPLAY_H
#define BSP_DISPLAY_H

#include <stdint.h>

/* 基线：2026-09-21 TEL、DS12232 Rev5第41页；高有效段选及NPN低侧位选。 */
#define BSP_DISPLAY_SEGMENT_A_PIN 9U
#define BSP_DISPLAY_SEGMENT_B_PIN 8U
#define BSP_DISPLAY_SEGMENT_C_PIN 7U
#define BSP_DISPLAY_SEGMENT_D_PIN 6U
#define BSP_DISPLAY_SEGMENT_E_PIN 5U
#define BSP_DISPLAY_SEGMENT_F_PIN 4U
#define BSP_DISPLAY_SEGMENT_G_PIN 3U
#define BSP_DISPLAY_DP_PIN 6U
#define BSP_DISPLAY_DIG1_PIN 4U
#define BSP_DISPLAY_DIG2_PIN 3U
#define BSP_DISPLAY_DIG3_PIN 2U
#define BSP_DISPLAY_DIG4_PIN 5U
#define BSP_DISPLAY_SEGMENT_COUNT 7U
#define BSP_DISPLAY_DIGIT_COUNT 4U
#define BSP_DISPLAY_SEGMENT_MASK ((1UL << BSP_DISPLAY_SEGMENT_A_PIN) | (1UL << BSP_DISPLAY_SEGMENT_B_PIN) | (1UL << BSP_DISPLAY_SEGMENT_C_PIN) | (1UL << BSP_DISPLAY_SEGMENT_D_PIN) | (1UL << BSP_DISPLAY_SEGMENT_E_PIN) | (1UL << BSP_DISPLAY_SEGMENT_F_PIN) | (1UL << BSP_DISPLAY_SEGMENT_G_PIN))
#define BSP_DISPLAY_DP_MASK (1UL << BSP_DISPLAY_DP_PIN)
#define BSP_DISPLAY_DIGIT_MASK ((1UL << BSP_DISPLAY_DIG1_PIN) | (1UL << BSP_DISPLAY_DIG2_PIN) | (1UL << BSP_DISPLAY_DIG3_PIN) | (1UL << BSP_DISPLAY_DIG4_PIN))
#define BSP_DISPLAY_PORT_D_MASK (BSP_DISPLAY_DIGIT_MASK | BSP_DISPLAY_DP_MASK)
/* RM0444 GPIO：每个MODER/OSPEEDR/PUPDR字段2位，MODER=01表示通用输出。 */
#define BSP_DISPLAY_GPIO_FIELD_WIDTH 2U
#define BSP_DISPLAY_GPIO_FIELD_MASK 3UL
#define BSP_DISPLAY_GPIO_MODE_OUTPUT 1UL
/* RM0444 GPIOx_BSRR的BR[15:0]位于[31:16]；字形bit7表示DP。 */
#define BSP_DISPLAY_BSRR_RESET_SHIFT 16U
#define BSP_DISPLAY_GLYPH_DP 0x80U
#define BSP_DISPLAY_PORT_PIN_COUNT 16U

/** @brief 进入短临界区，保护共享GPIO配置和Port状态。
 * @return 进入前PRIMASK状态，必须交给对应的ExitCritical恢复。 */
uint32_t BSP_Display_EnterCritical(void);
/** @brief 恢复进入临界区前的中断状态。
 * @param saved_mask 对应EnterCritical返回的PRIMASK。 */
void BSP_Display_ExitCritical(uint32_t saved_mask);
/** @brief 开启显示GPIO时钟，预置关闭电平并配置推挽极低速无上下拉输出。
 * @return 0表示配置回读一致，非0表示时钟或配置回读失败。 */
int BSP_Display_Init(void);
/** @brief 立即先关闭全部位选，再关闭所有段选和小数点。 */
void BSP_Display_Blank(void);
/** @brief 先关位选再换段码，最后只选通指定一位；空白字形不选通。
 * @param index 左到右位置，0至3。
 * @param glyph bit0至6表示A至G，bit7表示DP，高有效。
 * @return 0表示输出完成，非0表示位置非法且未改变输出。 */
int BSP_Display_Output(uint8_t index, uint8_t glyph);

#endif
