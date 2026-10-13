#ifndef APP_ULTRASONIC_TX_H
#define APP_ULTRASONIC_TX_H
#include "dev_common.h"

/**
 * @brief 显式初始化发射设备并启动第1路40kHz连续输出，不创建任务或定时器。
 * @return 设备启动成功DEV_OK，接口不可用DEV_NOT_READY，其他结果保留设备错误。
 */
DevStatus AppUltrasonicTx_Start(void);

/**
 * @brief 通过Device契约关闭发射，供业务停止及调度器异常返回处理使用。
 * @return 设备关闭结果；尚未初始化DEV_NOT_READY。
 */
DevStatus AppUltrasonicTx_Stop(void);

#endif
