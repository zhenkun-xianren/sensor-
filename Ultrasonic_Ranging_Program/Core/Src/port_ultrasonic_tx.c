#include <stdbool.h>
#include "port_ultrasonic_tx.h"
#include "bsp_ultrasonic_tx.h"

static DevStatus port_ultrasonic_tx_init(DevUltrasonicTx *self);
static DevStatus port_ultrasonic_tx_start_continuous(DevUltrasonicTx *self);
static DevStatus port_ultrasonic_tx_stop(DevUltrasonicTx *self);
static DevStatus port_ultrasonic_tx_start_burst(DevUltrasonicTx *self, uint16_t cycleCount);

static DevUltrasonicTx s_device = {
    port_ultrasonic_tx_init,
    port_ultrasonic_tx_start_continuous,
    port_ultrasonic_tx_stop,
    port_ultrasonic_tx_start_burst
}; // 唯一第1路设备，在定义处绑定全部方法。
static bool s_initialized = false; // 只表示显式初始化成功，不表示负载电压已实测。

/**
 * @brief 获取静态接口；保持初始化和运行状态不变。
 * @return 唯一第1路设备接口。
 */
DevUltrasonicTx *GetUltrasonicTx(void)
{
    return &s_device;
}

/**
 * @brief 检查对象后显式初始化板级发射资源。
 * @param self 本模块的唯一设备对象。
 * @return 对象错误DEV_ARGUMENT_ERROR，其他结果由BSP返回。
 */
static DevStatus port_ultrasonic_tx_init(DevUltrasonicTx *self)
{
    DevStatus result; // 保存板级初始化结果，成功后才允许调用发射方法。
    if (self != &s_device) return DEV_ARGUMENT_ERROR;
    result = BSP_UltrasonicTx_Init();
    s_initialized = (result == DEV_OK);
    return result;
}

/**
 * @brief 在初始化成功后启动固定频率的连续发射。
 * @param self 本模块的唯一设备对象。
 * @return 对象错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY，其他结果由BSP返回。
 */
static DevStatus port_ultrasonic_tx_start_continuous(DevUltrasonicTx *self)
{
    if (self != &s_device) return DEV_ARGUMENT_ERROR;
    if (!s_initialized) return DEV_NOT_READY;
    return BSP_UltrasonicTx_StartContinuous();
}

/**
 * @brief 在初始化成功后关闭发射或取消正在进行的唤醒。
 * @param self 本模块的唯一设备对象。
 * @return 对象错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY，其他结果由BSP返回。
 */
static DevStatus port_ultrasonic_tx_stop(DevUltrasonicTx *self)
{
    if (self != &s_device) return DEV_ARGUMENT_ERROR;
    if (!s_initialized) return DEV_NOT_READY;
    return BSP_UltrasonicTx_Stop();
}

/**
 * @brief 检查对象及初始化状态后启动有限周期发射。
 * @param self 本模块唯一设备对象。
 * @param cycleCount 脉冲串周期数，范围1至256。
 * @return 参数错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY，其余由BSP返回。
 */
static DevStatus port_ultrasonic_tx_start_burst(DevUltrasonicTx *self, uint16_t cycleCount)
{
    if (self != &s_device) return DEV_ARGUMENT_ERROR;
    if (!s_initialized) return DEV_NOT_READY;
    return BSP_UltrasonicTx_StartBurst(cycleCount);
}
