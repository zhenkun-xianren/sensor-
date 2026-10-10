#ifndef APP_MAIN_H
#define APP_MAIN_H

#include "dev_modbus_slave.h"
#include "app_flame_detection.h"

/** @brief 调试器可读的最近通信状态；发送提交成功不代表接收端收到。 */
extern volatile DevModbusSlaveStatus g_app_communication_status;
/** @brief 建立裸机应用状态，按启动配置初始化检测通道并初始化独占原始帧通信。
 * @details 仅在应用启动调用，建立首分钟并清零次数与历史；传感器配置结果分别保存在各路状态中。
 *          通信故障不阻止已成功配置通道的本地事件计数。
 *          传感器配置及最多5次额外重试全部在返回前完成，每次重试至少间隔1秒。
 * @param now_ms 板级传入的当前毫秒时间。
 * @param timing 固件入口提供的实际取时与等待回调，必须完整有效。
 * @return 仅返回原始帧通信初始化结果或计时参数错误，不代表三路传感器均配置成功。 */
DevModbusSlaveStatus App_Main_Init(uint32_t now_ms, const AppFlameInitTiming *timing);
/** @brief 串行调度分钟事件计数，上报三路次数、独立初始化状态及清除结果。
 * @param now_ms 板级传入的当前毫秒时间。 */
void App_Main_Poll(uint32_t now_ms);
/** @brief 应用参数接入入口，重新配置一路但保留本分钟次数及历史数据。
 * @details 每次调用只配置一次，失败直接结束，不安排自动重试；额外重试次数重置为0。
 * @param channel U22、U23或U24通道。
 * @param config 已由用户确定的完整内置检测配置。
 * @param now_ms 板级或调用者传入的毫秒时间。
 * @return 本次配置及回读结果，最终结果通过状态帧上报。 */
DevFlameResult App_Main_ConfigureFlame(DevFlameChannel channel, const DevFlameConfig *config, uint32_t now_ms);

#endif
