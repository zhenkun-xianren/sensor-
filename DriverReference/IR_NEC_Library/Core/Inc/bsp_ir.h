#ifndef BSP_IR_H
#define BSP_IR_H

#include "bsp_board.h"

#define BSP_IR_TIMER_COUNTER_HZ 1000000UL
#define BSP_IR_TIMER_PRESCALER ((BSP_BOARD_APB1_TIMER_HZ / BSP_IR_TIMER_COUNTER_HZ) - 1UL)
#define BSP_IR_TIMER_PERIOD_TICKS 65536UL
#define BSP_IR_EXTI_IRQ_PRIORITY 6U

void BSP_Ir_Init(void);
void BSP_Ir_IrqHandler(void);

#endif
