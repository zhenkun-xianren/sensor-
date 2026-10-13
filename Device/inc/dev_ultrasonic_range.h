#ifndef DEV_ULTRASONIC_RANGE_H
#define DEV_ULTRASONIC_RANGE_H

#include <stdint.h>
#include <stdbool.h>
#include "dev_common.h"

/** @brief 单换能器反射测距状态；无效结果不能作为零距离使用。 */
typedef enum {
    DEV_ULTRASONIC_RANGE_STOPPED = 0, /**< 周期测量或设备已停止。 */
    DEV_ULTRASONIC_RANGE_IDLE, /**< 已初始化，尚未发起测量。 */
    DEV_ULTRASONIC_RANGE_MEASURING, /**< 当前测量正在发射、消隐或等待回波。 */
    DEV_ULTRASONIC_RANGE_VALID, /**< 已捕获符合周期资格的回波且距离在范围内。 */
    DEV_ULTRASONIC_RANGE_NO_ECHO, /**< 接收截止前没有取得合格回波。 */
    DEV_ULTRASONIC_RANGE_OUT_OF_RANGE, /**< 校正后时间或距离超出有效范围。 */
    DEV_ULTRASONIC_RANGE_CAPTURE_OVERFLOW, /**< 硬件捕获覆盖或计数回绕，本次结果无效。 */
    DEV_ULTRASONIC_RANGE_IO_ERROR, /**< 外设配置、资源或受控关闭失败。 */
    DEV_ULTRASONIC_RANGE_LISTENING /**< 持续只接收测试；没有发射起点，距离始终无效。 */
} DevUltrasonicRangeState;

/** @brief 可调的单换能器测距参数；重复初始化仅接受相同配置。 */
typedef struct {
    uint16_t burst_cycles; /**< 发射周期数，1至256；本板消隐必须长于整串发射。 */
    uint16_t threshold_code; /**< 比较门限的12位码，1至4094；电压取决于实际参考。 */
    uint32_t blanking_us; /**< 从发射起点到接收放开的时间，单位us。 */
    uint32_t listen_timeout_us; /**< 从发射起点计的接收截止时间，最大65000us。 */
    uint32_t max_distance_mm; /**< 有效距离上限，单位mm，范围1至10000。 */
    uint32_t sound_speed_mm_s; /**< 声速，单位mm/s，允许300000至400000。 */
    int32_t time_offset_us; /**< 从原始往返时间扣除的延时校正，范围正负10000us。 */
    bool receive_only; /**< 真为持续只接收电路测试，假为单次发射测距；只接收忽略消隐/截止但保留参数范围。 */
} DevUltrasonicRangeConfig;

/** @brief 单次结果；仅valid为真时distance_mm有效，原始时间便于示波器比对。 */
typedef struct {
    uint32_t distance_mm; /**< 有效单程距离，单位mm；无效时清零。 */
    uint32_t echo_time_us; /**< 合格回波首沿距发射起点的原始往返时间，单位us。 */
    uint32_t sequence; /**< 单次启动序号，自然回绕，用于区分新旧结果。 */
    uint32_t captured_edges; /**< 测距为本次消隐后上升沿数；只接收为本次启动后的累计数，自然回绕。 */
    uint32_t rejected_edges; /**< 周期资格被打断时丢弃的候选边沿数。 */
    uint32_t overflow_count; /**< 测距为覆盖/回绕失效数；只接收为捕获覆盖次数，覆盖后重置周期候选继续接收。 */
    DevUltrasonicRangeState state; /**< 本次测量状态；超时、停止和故障分别表示。 */
    bool valid; /**< 距离是否可用；获取接口不改变它。 */
    uint32_t last_period_us; /**< 最近相邻捕获上升沿间隔，单位us；首沿或回绕后未建立周期时为0，须结合捕获数是否增长。 */
    uint32_t qualified_groups; /**< 累计非重叠的3边沿资格组数，要求相邻间隔20至30us；只接收不据此停止或发布距离。 */
    uint32_t timer_wrap_count; /**< 只接收连续计数的正常回绕次数，自然回绕；正常回绕不关闭接收。 */
} DevUltrasonicRangeResult;

/** @brief 单换能器设备对象，不暴露板级引脚、寄存器或RTOS句柄。 */
typedef struct DevUltrasonicRange DevUltrasonicRange;

/** @brief 方法由Port在静态定义处绑定；获取后须显式初始化。 */
struct DevUltrasonicRange {
    /**
     * @brief 初始化测距资源；成功保持停止，重复相同配置不重置状态。
     * @param self 本模块设备对象。
     * @param config 非空参数对象，按各字段范围填写。
     * @return 成功DEV_OK，参数或不同的重复配置DEV_ARGUMENT_ERROR，资源/配置失败DEV_IO_ERROR。
     */
    DevStatus (*init)(DevUltrasonicRange *self, const DevUltrasonicRangeConfig *config);
    /**
     * @brief 按配置启动单次反射测量或持续只接收；重复进行中请求不重置起点或计数。
     * @param self 已初始化设备对象。
     * @return 启动成功DEV_OK，正测量DEV_PENDING，对象错误DEV_ARGUMENT_ERROR，
     *         未初始化或取消DEV_NOT_READY，唤醒超时DEV_TIMEOUT，板级失败DEV_IO_ERROR。
     */
    DevStatus (*startMeasurement)(DevUltrasonicRange *self);
    /**
     * @brief 复制完整结果；测距完成时计算距离，只接收提供累计脉冲诊断并保持距离无效。
     * @param self 已初始化设备对象。
     * @param result 非空结果对象；参数/未就绪失败不修改它。
     * @return 完成或停止DEV_OK，测距进行中或持续只接收DEV_PENDING，参数错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY。
     */
    DevStatus (*readResult)(DevUltrasonicRange *self, DevUltrasonicRangeResult *result);
    /**
     * @brief 无等待取消单次测量、关闭发射并恢复接收钳位；重复停止保持停止。
     * @param self 已初始化设备对象。
     * @return 完成DEV_OK，对象错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY，关闭失败DEV_IO_ERROR。
     */
    DevStatus (*stop)(DevUltrasonicRange *self);
};

/**
 * @brief 仅取得唯一静态接口，不操作硬件、分配内存或清除历史状态。
 * @return 第1路单换能器反射测距设备对象。
 */
DevUltrasonicRange *GetUltrasonicRange(void);

#endif
