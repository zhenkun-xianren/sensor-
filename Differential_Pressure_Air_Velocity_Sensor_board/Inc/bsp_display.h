#ifndef BSP_DISPLAY_H
#define BSP_DISPLAY_H

#include <stdint.h>

#define BSP_DISPLAY_DIGIT_COUNT 4U
#define BSP_DISPLAY_GLYPH_DP_MASK 0x80U

/**
 * @brief 进入短临界区，保护Port状态和板级输出顺序。
 * @return 进入前的中断屏蔽状态，须交给对应的退出接口恢复。
 */
uint32_t BSP_Display_EnterCritical(void);

/**
 * @brief 恢复进入前的中断屏蔽状态，不擅自开启已关闭的中断。
 * @param saved_mask 进入临界区接口返回的状态。
 */
void BSP_Display_ExitCritical(uint32_t saved_mask);

/**
 * @brief 初始化显示硬件并立即关闭输出，帧缓冲及扫描状态由Port维护。
 * @return 0表示初始化完成，非0表示失败；配置前提与检测范围由具体BSP说明。
 */
int BSP_Display_Init(void);

/**
 * @brief 先关闭全部位选，再关闭所有段选和小数点。
 */
void BSP_Display_Blank(void);

/**
 * @brief 先关闭位选，再输出段码，最后选通指定一位；空白字形不选通。
 * @param index 左到右的数码位编号，范围为0至3。
 * @param glyph 位0至6对应A至G，位7对应小数点，高有效。
 * @return 0表示输出操作完成，非0表示失败；非法编号不得改变输出。
 */
int BSP_Display_Output(uint8_t index, uint8_t glyph);

#endif
