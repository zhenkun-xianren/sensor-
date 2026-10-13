#ifndef BSP_RTOS_H
#define BSP_RTOS_H

/** @brief 将系统 Tick 转交内核；调度器尚未启动时不调用内核 Tick。 */
void BSP_RtosTick(void);

#endif
