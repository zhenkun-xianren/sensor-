#ifndef BSP_IR_H
#define BSP_IR_H
#include <stdint.h>
#include "bsp_board.h"
#define BSP_IR_INPUT_PIN 15U
#define BSP_IR_INPUT_MASK (1UL << BSP_IR_INPUT_PIN)
#define BSP_IR_GPIO_FIELD_BITS 4U
#define BSP_IR_GPIO_CRH_SHIFT ((BSP_IR_INPUT_PIN - 8U) * BSP_IR_GPIO_FIELD_BITS)
#define BSP_IR_GPIO_FIELD_MASK 0xFUL
#define BSP_IR_GPIO_INPUT_FLOATING 0x4UL
#define BSP_IR_EXTICR_INDEX 3U
#define BSP_IR_TIMER_COUNTER_HZ 1000000UL
#define BSP_IR_TIMER_PRESCALER ((BSP_BOARD_APB1_TIMER_HZ / BSP_IR_TIMER_COUNTER_HZ) - 1UL)
#define BSP_IR_TIMER_PERIOD_TICKS 65536UL
#define BSP_IR_IRQ_PRIORITY 6U
/** @brief 边沿采集向协议层交付已结束脉冲的回调。 */
typedef void (*BspIrPulseCallback)(uint8_t level, uint32_t duration_us, uint32_t now_us);
/**
 * @brief 用寄存器配置 PA15、EXTI15 和 TIM3 微秒时基。
 * @param callback 接收脉冲的非空协议层回调。
 * @return 成功返回1，参数无效返回0。
 */
uint8_t BSP_Ir_Init(BspIrPulseCallback callback);
/**
 * @brief 保护任务读取与中断写入之间的短临界区。
 * @return 进入临界区前的 PRIMASK 值。
 */
uint32_t BSP_Ir_Lock(void);
/**
 * @brief 恢复进入临界区前的中断屏蔽状态。
 * @param mask BSP_Ir_Lock 返回的 PRIMASK 值。
 */
void BSP_Ir_Unlock(uint32_t mask);
#endif
