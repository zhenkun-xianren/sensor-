#ifndef APP_MODBUS_H
#define APP_MODBUS_H

#include <stdint.h>

typedef struct DevModbus DevModbus;

typedef enum {
    APP_MODBUS_TX_NOT_RUN = 0, /**< 周期任务尚未执行过发送。 */
    APP_MODBUS_TX_OK, /**< 最近一次 Modbus 请求发送成功。 */
    APP_MODBUS_TX_ERROR /**< 最近一次 Modbus 请求发送失败。 */
} AppModbusTxStatus;

extern volatile uint32_t g_app_modbus_task_heartbeat; /**< Modbus APP 静态任务每秒递增的调试计数。 */
extern volatile uint32_t g_app_modbus_timer_count; /**< Modbus APP 软件定时器每秒递增的调试计数。 */
extern volatile uint32_t g_app_modbus_tx_count; /**< 已成功发送的 Modbus 请求帧数量。 */
extern volatile AppModbusTxStatus g_app_modbus_tx_status; /**< 最近一次 Modbus 请求发送状态。 */

/**
 * @brief 创建 Modbus APP 静态周期任务和软件定时器。
 * @param modbus Modbus 设备接口对象；串口初始化失败时任务仍可创建，但发送会报告失败。
 * @return 任务、定时器创建并成功启动返回 1，否则返回 0。
 */
uint8_t App_Modbus_Start(DevModbus *modbus);

#endif /* APP_MODBUS_H */
