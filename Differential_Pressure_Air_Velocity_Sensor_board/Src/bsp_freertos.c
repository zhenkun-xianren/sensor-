#include "FreeRTOS.h"
#include "task.h"
#include "bsp_board.h"
static StaticTask_t s_idle_control; // 保存静态Idle任务控制块。
static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE]; // 保存Idle任务栈，单位为字。
static StaticTask_t s_timer_control; // 保存静态Timer Service任务控制块。
static StackType_t s_timer_stack[configTIMER_TASK_STACK_DEPTH]; // 保存定时器服务任务栈，单位为字。
/** @brief 向上游内核提供静态Idle任务内存。
 * @param task_buffer 接收任务控制块地址。
 * @param stack_buffer 接收栈地址。
 * @param stack_size 接收栈字数。 */
void vApplicationGetIdleTaskMemory(StaticTask_t **task_buffer, StackType_t **stack_buffer, uint32_t *stack_size)
{
    *task_buffer = &s_idle_control; *stack_buffer = s_idle_stack; *stack_size = configMINIMAL_STACK_SIZE;
}
/** @brief 向上游内核提供静态Timer Service任务内存。
 * @param task_buffer 接收任务控制块地址。
 * @param stack_buffer 接收栈地址。
 * @param stack_size 接收栈字数。 */
void vApplicationGetTimerTaskMemory(StaticTask_t **task_buffer, StackType_t **stack_buffer, uint32_t *stack_size)
{
    *task_buffer = &s_timer_control; *stack_buffer = s_timer_stack; *stack_size = configTIMER_TASK_STACK_DEPTH;
}
/** @brief 静态框架出现栈溢出时进入受控故障处理。
 * @param task 出现栈溢出的任务句柄。
 * @param name 任务名称。 */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task; (void)name; BSP_Board_Fault(BSP_FAULT_STACK);
}
/** @brief 内核分配失败时停止启动；应用正常路径仅使用静态创建。 */
void vApplicationMallocFailedHook(void) { BSP_Board_Fault(BSP_FAULT_MEMORY); }
