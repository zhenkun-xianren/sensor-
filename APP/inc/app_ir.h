#ifndef APP_IR_H
#define APP_IR_H
#include "app_main.h"
#include "dev_ir_remote.h"
#include "dev_display.h"
#include "dev_common.h"
#include <stdbool.h>
/** @brief 保存最近完整事件；仅供当前单任务写入及调试器观察。 */
extern volatile IrKeyEvent g_ir_last_event;
/** @brief 保存是否已接收到有效按键事件。 */
extern volatile bool g_ir_has_event;
/** @brief 保存Device接收队列累计丢弃数量。 */
extern volatile uint32_t g_ir_overflow_count;
/** @brief 保存最近读取状态。 */
extern volatile DevIrResult g_ir_status;
/** @brief 保存最近数字显示请求结果。 */
extern volatile DevDisplayStatus g_ir_display_status;
/** @brief 保存最近继电器驱动请求结果，不代表机械触点反馈。 */
extern volatile DevStatus g_ir_relay_status;
/** @brief 保存最近光耦开关请求结果，不代表实际负载反馈。 */
extern volatile DevStatus g_ir_optocoupler_status;
/** @brief 保存最近蜂鸣器开关请求结果。 */
extern volatile DevStatus g_ir_buzzer_status;
/** @brief 保存蜂鸣请求和定时器命令结果。 */
extern volatile AppStatus g_ir_feedback_status;
/** @brief 初始化红外Device，并核对显示、继电器、光耦和蜂鸣器接口。
 * @return 成功APP_OK，红外失败APP_ERROR_IR，显示接口缺失APP_ERROR_DISPLAY，执行器接口缺失APP_ERROR_ACTUATOR。 */
AppStatus App_Ir_Init(void);
/** @brief 创建100ms单次蜂鸣定时器及每10ms处理数字1至9的静态任务。
 * @return 成功APP_OK，定时器创建失败APP_ERROR_TIMER，任务创建失败APP_ERROR_TASK。 */
AppStatus App_Ir_Start(void);
#endif
