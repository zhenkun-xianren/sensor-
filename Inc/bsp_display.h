#ifndef BSP_DISPLAY_H
#define BSP_DISPLAY_H

#include <stdint.h>

#define BSP_DISPLAY_DIGIT_COUNT 4U
#define BSP_DISPLAY_GLYPH_DP_MASK 0x80U

/**
 * @brief 初始化显示 GPIO 和扫描状态。
 * @return 引脚与安全状态检查通过返回 1，否则返回 0。
 */
uint8_t BSP_Display_Init(void);

/**
 * @brief 原子更新四位数码管的显示字形帧。
 * @param frame 按 DIG1 至 DIG4 顺序排列的四个字形；每个字形的位 0 至 6 对应 A 至 G，位 7 对应 DP。
 */
void BSP_Display_SetFrame(const uint8_t frame[BSP_DISPLAY_DIGIT_COUNT]);

/**
 * @brief 更新指定数码位的显示字形。
 * @param index 数码位索引，0 至 3 依次对应 DIG1 至 DIG4。
 * @param glyph 七段字形位图，位 0 至 6 对应 A 至 G，位 7 对应 DP。
 */
void BSP_Display_SetDigit(uint8_t index, uint8_t glyph);

/**
 * @brief 扫描并刷新下一位数码管。
 */
void BSP_Display_ScanStep(void);

#endif
