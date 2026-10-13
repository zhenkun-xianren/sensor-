#ifndef APP_MODBUS_H
#define APP_MODBUS_H
#include <stdint.h>
#include "dev_modbus.h"

/* 用户确认的只发送测试参数：协议零基地址，无从站应答验证。 */
#define APP_MODBUS_PERIOD_MS           1000U
#define APP_MODBUS_SLAVE_ADDRESS       1U
#define APP_MODBUS_REGISTER_ADDRESS    0x0000U
#define APP_MODBUS_REGISTER_VALUE      0x0001U

/** @brief 任务上下文可读取的本地发送诊断，不代表从站响应。 */
typedef struct {
    uint32_t trigger_count; // 累计定时器通知次数。
    uint32_t send_count; // 完整UART发送成功的帧数。
    uint32_t failure_count; // 本地发送或再次初始化失败的次数。
    uint32_t task_stack_free_words; // 发送任务最小剩余栈，单位32位字。
    DevModbusStatus last_status; // 最近一次本地初始化或发送结果。
} AppModbusSnapshot;

/**
 * @brief 初始化设备并创建静态发送任务和软件定时器，首次发送在启动后约1秒。
 * @return 0表示启动成功，非0表示重复启动、设备初始化或RTOS对象创建失败。
 */
int AppModbus_Start(void);

/**
 * @brief 在短临界区内复制一致诊断快照，只供任务上下文使用。
 * @param output 非空诊断输出对象。
 * @return 0表示成功，非0表示参数错误或模块未完整启动。
 */
int AppModbus_GetSnapshot(AppModbusSnapshot *output);
#endif
