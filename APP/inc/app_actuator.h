#ifndef APP_ACTUATOR_H
#define APP_ACTUATOR_H
#include "app_main.h"
#include "dev_actuator.h"
/** @brief 保存初始化后的输出锁存逻辑快照，不代表实物反馈。 */
extern volatile DevActuatorState g_actuator_initial_state;
/** @brief 最近一次两灯逻辑控制结果，不代表实物反馈。 */
extern volatile DevStatus g_led_blink_status;
/** @brief 初始化执行器并确认默认关闭逻辑状态。
 * @return 成功APP_OK，否则APP_ERROR_ACTUATOR。 */
AppStatus App_Actuator_Init(void);
/** @brief 启动报警灯和电源灯每隔1秒交换亮灭状态。
 * @return 成功APP_OK，控制失败APP_ERROR_ACTUATOR，定时器失败APP_ERROR_TIMER。 */
AppStatus App_Actuator_Start(void);
#endif
