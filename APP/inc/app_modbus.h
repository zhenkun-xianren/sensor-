#ifndef APP_MODBUS_H
#define APP_MODBUS_H

#include <stdint.h>

extern volatile uint32_t g_app_modbus_task_heartbeat; /**< Modbus APP 静态任务每秒递增的调试计数。 */
extern volatile uint32_t g_app_modbus_timer_count; /**< Modbus APP 软件定时器每秒递增的调试计数。 */

/**
 * @brief 创建 Modbus APP 静态周期任务和软件定时器；本阶段不发送数据。
 * @return 任务、定时器创建并成功启动返回 1，否则返回 0。
 */
uint8_t App_Modbus_Start(void);

#endif /* APP_MODBUS_H */
