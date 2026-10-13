#ifndef BSP_TIME_H
#define BSP_TIME_H
#include <stdint.h>

#define BSP_TIME_TICK_HZ 1000UL /* 1ms计时，与当前FreeRTOS Tick保持一致。 */

/**
 * @brief 在启动阶段使用当前HCLK建立1ms SysTick，重新调用会清零毫秒计数。
 * @return 配置成功返回0，主频或重装值无效返回-1；不得在运行期调用。
 */
int BSP_Time_Init(void);

/**
 * @brief 读取已处理SysTick中断累计的毫秒数，计数自然按32位回绕。
 * @return 毫秒计数；超时使用无符号差值，依赖中断未被长时间屏蔽。
 */
uint32_t BSP_Time_GetMs(void);

/** @brief 由唯一SysTick中断每1ms调用一次，推进BSP计时，不主动访问RTOS。 */
void BSP_Time_Tick(void);

#endif
