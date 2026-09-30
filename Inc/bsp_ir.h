#ifndef BSP_IR_H
#define BSP_IR_H

#include "bsp_board.h"
#include <stdint.h>

#define BSP_IR_TIMER_COUNTER_HZ             1000000UL
#define BSP_IR_TIMER_PRESCALER               ((BSP_BOARD_APB1_TIMER_HZ / BSP_IR_TIMER_COUNTER_HZ) - 1UL)
#define BSP_IR_TIMER_PERIOD_TICKS            65536UL
#define BSP_IR_TIMER_PRESCALER_MAX           0xFFFFUL
#define BSP_IR_EXTI_IRQ_PRIORITY              6U
#define BSP_IR_GPIO_PA8_PIN                   8U
#define BSP_IR_GPIO_CRH_PA8_SHIFT             0U
#define BSP_IR_GPIO_CRH_PA8_CONFIG_MASK       0xFUL
#define BSP_IR_GPIO_INPUT_PULLUP_CONFIG      0x8UL
#define BSP_IR_GPIO_PA8_MASK                  (1UL << BSP_IR_GPIO_PA8_PIN)

/**
 * @brief 红外边沿回调函数类型。
 * @param level 本次边沿之前的红外脉冲电平。
 * @param duration_us 本次脉冲持续时间，单位为微秒。
 */
typedef void (*BspIrPulseCallback)(uint8_t level, uint16_t duration_us);

/**
 * @brief 配置 PA8 上拉、TIM3 微秒计数和 EXTI8 双边沿捕获。
 * @param pulse_callback 收到有效输入边沿间隔时调用的协议层回调。
 * @return GPIO、定时器和中断配置及寄存器核对均成功返回 1，否则返回 0。
 */
uint8_t BSP_Ir_Init(BspIrPulseCallback pulse_callback);

/**
 * @brief 处理中断线 8 的电平边沿并向协议层转交脉冲时长。
 */
void BSP_Ir_IrqHandler(void);

#endif /* BSP_IR_H */
