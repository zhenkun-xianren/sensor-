#include <stddef.h>
#include "port_ultrasonic_range.h"
#include "bsp_ultrasonic_range.h"

static DevStatus port_ultrasonic_range_init(DevUltrasonicRange *self, const DevUltrasonicRangeConfig *config);
static DevStatus port_ultrasonic_range_start_measurement(DevUltrasonicRange *self);
static DevStatus port_ultrasonic_range_read_result(DevUltrasonicRange *self, DevUltrasonicRangeResult *result);
static DevStatus port_ultrasonic_range_stop(DevUltrasonicRange *self);

static DevUltrasonicRange s_device = {
    port_ultrasonic_range_init,
    port_ultrasonic_range_start_measurement,
    port_ultrasonic_range_read_result,
    port_ultrasonic_range_stop
}; // 静态绑定唯一传播测量对象，Get不补写方法。
static bool s_initialized = false; // 仅表示本设备显式配置成功。
static DevUltrasonicRangeConfig s_config; // 保存已验证参数，用于距离及延时校正。

/** @brief 只取得静态设备接口，保留原配置和历史结果。
 * @return 唯一传播测量对象。 */
DevUltrasonicRange *GetUltrasonicRange(void)
{
    return &s_device;
}

/** @brief 显式配置板级资源，成功后记录换算参数。
 * @param self 本模块唯一对象。
 * @param config 非空设备配置。
 * @return 参数错误DEV_ARGUMENT_ERROR，其他结果由BSP返回。 */
static DevStatus port_ultrasonic_range_init(DevUltrasonicRange *self, const DevUltrasonicRangeConfig *config)
{
    DevStatus status; // 保存配置结果，失败不抹去旧的成功配置。
    if (self != &s_device || config == NULL) return DEV_ARGUMENT_ERROR;
    status = BSP_UltrasonicRange_Init(config);
    if (status == DEV_OK) {
        s_config = *config;
        s_initialized = true;
    }
    return status;
}

/** @brief 校验对象及就绪后，按配置发起一次测距或持续只接收。
 * @param self 本模块唯一对象。
 * @return 参数错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY，其他结果由BSP返回。 */
static DevStatus port_ultrasonic_range_start_measurement(DevUltrasonicRange *self)
{
    if (self != &s_device) return DEV_ARGUMENT_ERROR;
    if (!s_initialized) return DEV_NOT_READY;
    return BSP_UltrasonicRange_StartMeasurement();
}

/** @brief 复制脉冲结果；只接收保持距离无效，按公共传播程数把合格时间换算为毫米路径长度。
 * @param self 本模块唯一对象。
 * @param result 保存完整结果的非空对象。
 * @return 完成DEV_OK，进行中DEV_PENDING，参数错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY。 */
static DevStatus port_ultrasonic_range_read_result(DevUltrasonicRange *self, DevUltrasonicRangeResult *result)
{
    DevStatus status; // BSP复制结果时给出的等待状态。
    int64_t corrected_us; // 扣除系统延时后的传播时间，可识别非法负时间。
    uint64_t distance; // 64位乘法中间结果，避免时间乘声速溢出。
    if (self != &s_device || result == NULL) return DEV_ARGUMENT_ERROR;
    if (!s_initialized) return DEV_NOT_READY;
    status = BSP_UltrasonicRange_ReadResult(result);
    if (status != DEV_OK && status != DEV_PENDING) return status;
    result->distance_mm = 0UL;
    result->valid = false;
    if (s_config.receive_only) {
        result->echo_time_us = 0UL; /* 没有发射起点，不把外部脉冲当作传播时间。 */
        return status;
    }
    if (result->state == DEV_ULTRASONIC_RANGE_VALID) {
        corrected_us = (int64_t)result->echo_time_us - s_config.time_offset_us;
        if (corrected_us <= 0) result->state = DEV_ULTRASONIC_RANGE_OUT_OF_RANGE;
        else {
            distance = ((uint64_t)corrected_us * s_config.sound_speed_mm_s +
                        PORT_ULTRASONIC_RANGE_TIME_SCALE / 2ULL) / PORT_ULTRASONIC_RANGE_TIME_SCALE;
            if (distance > s_config.max_distance_mm) result->state = DEV_ULTRASONIC_RANGE_OUT_OF_RANGE;
            else {
                result->distance_mm = (uint32_t)distance;
                result->valid = true;
            }
        }
    }
    return status;
}

/** @brief 校验对象后无等待停止本设备。
 * @param self 本模块唯一对象。
 * @return 参数错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY，其他结果由BSP返回。 */
static DevStatus port_ultrasonic_range_stop(DevUltrasonicRange *self)
{
    if (self != &s_device) return DEV_ARGUMENT_ERROR;
    if (!s_initialized) return DEV_NOT_READY;
    return BSP_UltrasonicRange_Stop();
}
