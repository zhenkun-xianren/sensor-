#ifndef APP_MODBUS_H
#define APP_MODBUS_H
#include "app_main.h"
#include "dev_modbus.h"
#include "app_measurement.h"
/** @brief 保存Modbus初始化状态，不表示任何从站已执行命令。 */
extern volatile DevModbusStatus g_modbus_init_status;
/** @brief 最近一次电压帧的本地发送结果；若跳过发送则保持，不等待从站应答。 */
extern volatile DevModbusStatus g_modbus_send_status;
/** @brief 电压帧发送调用总次数，包括失败调用，不包含跳过的周期。 */
extern volatile uint32_t g_modbus_send_count;
/** @brief 本地完整发送次数，不表示从站已执行命令。 */
extern volatile uint32_t g_modbus_send_success_count;
/** @brief 因测量未就绪、失效或过期而跳过的发送周期数。 */
extern volatile uint32_t g_modbus_skip_count;
/** @brief 最近一次发送周期读取的测量状态，非OK表示跳过原因。 */
extern volatile AppMeasurementStatus g_modbus_measurement_status;
/** @brief 最近一次实际提交给发送接口的输入电压，单位mV。 */
extern volatile uint16_t g_modbus_last_millivolts;
/** @brief 初始化主站；周期发送由App_Modbus_Start启动。
 * @return 成功APP_OK，否则APP_ERROR_MODBUS。 */
AppStatus App_Modbus_Init(void);
/** @brief 调度器启动前创建静态任务及定时器，每1秒发送一次最新有效电压的0x06帧。
 * @return 成功APP_OK，设备未就绪APP_ERROR_MODBUS，任务失败APP_ERROR_TASK，定时器失败APP_ERROR_TIMER。 */
AppStatus App_Modbus_Start(void);
#endif
