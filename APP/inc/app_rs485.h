#ifndef APP_RS485_H
#define APP_RS485_H
#include <stdint.h>
#include "dev_modbus.h"
typedef enum {
    APP_RS485_PENDING = 0,
    APP_RS485_INITIALIZING,
    APP_RS485_RUNNING,
    APP_RS485_ERROR_RESOURCE,
    APP_RS485_ERROR_DEVICE,
    APP_RS485_ERROR_INIT,
    APP_RS485_ERROR_TIMER
} AppRs485Status;

extern volatile AppRs485Status g_app_rs485_status; // 模块启动与运行状态。
extern volatile DevModbusStatus g_app_rs485_last_result; // 最近结束的 Modbus 事务结果。
extern volatile uint16_t g_app_rs485_last_value; // 最近发起事务的无单位测试值。
extern volatile uint32_t g_app_rs485_attempt_count; // 已发起事务总数，允许自然回绕。
extern volatile uint32_t g_app_rs485_success_count; // 正常回显总数。
extern volatile uint32_t g_app_rs485_timeout_count; // 发送或应答超时总数。
extern volatile uint32_t g_app_rs485_error_count; // 除超时外的通信错误总数。
extern volatile uint32_t g_app_rs485_stack_free_words; // 通信任务历史最小剩余栈字数。
/**
 * @brief 调度器启动前创建静态通信任务和一秒软件定时器。
 * @return 创建成功返回 1，资源创建失败返回 0。
 */
uint8_t AppRs485_Start(void);
#endif

