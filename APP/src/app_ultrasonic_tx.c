#include <stddef.h>
#include "app_ultrasonic_tx.h"
#include "dev_ultrasonic_tx.h"

/**
 * @brief 按获取接口、显式初始化、检查结果、启动的顺序开启连续发射。
 * @return 成功DEV_OK，接口不可用DEV_NOT_READY，其他返回设备操作的原错误。
 */
DevStatus AppUltrasonicTx_Start(void)
{
    DevUltrasonicTx *device = GetUltrasonicTx(); // 仅取得静态接口，不代表硬件已经初始化。
    DevStatus result; // 保存初始化及启动的原始错误，关闭请求不覆盖它。
    if (device == NULL) return DEV_NOT_READY;
    result = device->init(device);
    if (result != DEV_OK) return result;
    result = device->startContinuous(device);
    if (result != DEV_OK) (void)device->stop(device);
    return result;
}

/**
 * @brief 使用唯一Device对象执行停止，不直接访问Port/BSP或寄存器。
 * @return 设备停止的原结果；接口不可用DEV_NOT_READY。
 */
DevStatus AppUltrasonicTx_Stop(void)
{
    DevUltrasonicTx *device = GetUltrasonicTx(); // 获取对象不重置初始化和运行状态。
    if (device == NULL) return DEV_NOT_READY;
    return device->stop(device);
}
