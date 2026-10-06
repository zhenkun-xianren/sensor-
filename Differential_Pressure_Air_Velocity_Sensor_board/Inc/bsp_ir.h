#ifndef BSP_IR_H
#define BSP_IR_H
#include <stdint.h>
#include "bsp_board.h"
#define BSP_IR_INPUT_PIN 8U
#define BSP_IR_INPUT_MASK (1UL << BSP_IR_INPUT_PIN)
#define BSP_IR_TIMER_COUNTER_HZ 1000000UL
#define BSP_IR_TIMER_PRESCALER ((BSP_BOARD_TIM2_HZ / BSP_IR_TIMER_COUNTER_HZ) - 1UL)
#define BSP_IR_TIMER_MAX_COUNT UINT16_MAX
#define BSP_IR_TIMER_WRAP_US ((uint64_t)UINT16_MAX + 1ULL)
#define BSP_IR_TIMER_VERIFY_POLLS 1024UL
#define BSP_IR_EXTI_IRQ_PRIORITY 2U
#define BSP_IR_EXTICR_INDEX 2U
#define BSP_IR_LEVEL_LOST 2U
#define BSP_IR_LEVEL_IDLE 3U
/** @brief 保持NEC Port的32位脉冲契约。
 * @param level 已结束电平，或LOST/IDLE标记。
 * @param duration_us 32位微秒间隔，过长时饱和。
 * @param end_us 自然回绕的32位结束时间戳。 */
typedef void (*BspIrPulseCallback)(uint8_t level, uint32_t duration_us, uint32_t end_us);
/** @brief 初始化PA8/EXTI8与TIM2，资源占用或核对失败不启用接收。
 * @param callback 非空的Port脉冲回调。
 * @return 成功BSP_BOARD_OK，否则BSP_BOARD_ERROR。 */
int BSP_Ir_Init(BspIrPulseCallback callback);
/** @brief 保存PRIMASK并进入短临界区。
 * @return 原PRIMASK。 */
uint32_t BSP_Ir_EnterCritical(void);
/** @brief 恢复原PRIMASK。
 * @param saved_mask 配对进入函数返回值。 */
void BSP_Ir_ExitCritical(uint32_t saved_mask);
/** @brief 仅处理EXTI8，不清除共享向量其他线路挂起位。 */
void BSP_Ir_IrqHandler(void);
#endif
