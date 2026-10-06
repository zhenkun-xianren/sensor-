#ifndef BSP_IR_H
#define BSP_IR_H

#include <stdint.h>
#include "bsp_board.h"

/* DS12232 Rev2第35页：LQFP64第50脚为PD0；网表IR1连接该脚。 */
#define BSP_IR_INPUT_PIN 0U
#define BSP_IR_INPUT_MASK (1UL << BSP_IR_INPUT_PIN)
#define BSP_IR_TIMER_COUNTER_HZ 1000000UL
#define BSP_IR_TIMER_PRESCALER ((BSP_BOARD_TIM2_HZ / BSP_IR_TIMER_COUNTER_HZ) - 1UL)
#define BSP_IR_TIMER_MAX_COUNT UINT32_MAX
#define BSP_IR_TIMER_VERIFY_POLLS 1024UL
#define BSP_IR_EXTI_IRQ_PRIORITY 2U
#define BSP_IR_LEVEL_LOST 2U
#define BSP_IR_LEVEL_IDLE 3U
#define BSP_IR_MAX_IDLE_WRAPS 2U

/* RM0444 12.5.6：EXTICR1的EXTI0端口编码0x03为GPIOD。 */
#define BSP_IR_EXTI_PORT_D 3UL

/**
 * @brief 板级脉冲回调，不得阻塞或操作动态内存。
 * @param level 脉冲电平；BSP_IR_LEVEL_LOST表示丢失，BSP_IR_LEVEL_IDLE表示计数器溢出时的空闲检查。
 * @param duration_us 脉冲微秒时长。
 * @param end_us 脉冲结束的32位微秒时间戳。
 */
typedef void (*BspIrPulseCallback)(uint8_t level, uint32_t duration_us, uint32_t end_us);

/**
 * @brief 通过寄存器初始化PD0、EXTI0与TIM2；IRQ在初始化成功后启用。
 * @param callback 接收脉冲的非空Port回调。
 * @return 0表示成功，非0表示时钟、资源占用、引脚锁定或配置核对失败。
 */
int BSP_Ir_Init(BspIrPulseCallback callback);

/**
 * @brief 进入短临界区并保留调用前的中断屏蔽状态。
 * @return 调用前的PRIMASK值，供配对退出使用。
 */
uint32_t BSP_Ir_EnterCritical(void);

/**
 * @brief 恢复进入临界区前的中断状态。
 * @param saved_mask 配对进入函数返回的PRIMASK值。
 */
void BSP_Ir_ExitCritical(uint32_t saved_mask);

/** @brief 处理本驱动EXTI0挂起位，不清除共享向量中的EXTI1挂起位。 */
void BSP_Ir_IrqHandler(void);

#endif
