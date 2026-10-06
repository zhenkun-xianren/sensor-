#ifndef APP_MEASUREMENT_H
#define APP_MEASUREMENT_H

#include <stdint.h>
#include <stdbool.h>
#include "app_main.h"
#include "dev_common.h"

/* 本板分压偏置及业务参数：原理图第1页，100k/150k/1M，REF5025标称2.5V。 */
#define APP_MEASUREMENT_PERIOD_MS 100U
#define APP_MEASUREMENT_STALE_MS 300U
#define APP_MEASUREMENT_SAMPLE_COUNT 32U
#define APP_MEASUREMENT_REFERENCE_MV 2500LL
#define APP_MEASUREMENT_SIGNAL_FACTOR 53LL
#define APP_MEASUREMENT_BIAS_FACTOR 3LL
#define APP_MEASUREMENT_INPUT_FACTOR 30LL
#define APP_MEASUREMENT_ROUND_DIVISOR 2LL
#define APP_MEASUREMENT_INPUT_MIN_MV 0L
#define APP_MEASUREMENT_INPUT_MAX_MV 5000L
#define APP_MEASUREMENT_BOUNDARY_TOLERANCE_MV 30L
#define APP_MEASUREMENT_REFERENCE_MIN_RAW 1U
#define APP_MEASUREMENT_SIGNAL_MIN_RAW 1U /* 零码属于信号下轨饱和，正常0V输入经偏置后不应为零码。 */
#define APP_MEASUREMENT_TASK_STACK_WORDS 256U
#define APP_MEASUREMENT_TASK_PRIORITY 1U
#define APP_MEASUREMENT_COMMAND_WAIT_TICKS 0U
#define APP_MEASUREMENT_NO_NOTIFICATION 0U
#define APP_MEASUREMENT_MILLISECONDS_PER_SECOND 1000UL

/** @brief 测量数据状态；只有OK允许显示数值及向RS485提交测量值。 */
typedef enum {
    APP_MEASUREMENT_OK = 0, /**< 本周期完整测量有效。 */
    APP_MEASUREMENT_NOT_READY, /**< 尚无完整结果或模块尚未初始化。 */
    APP_MEASUREMENT_ADC_ERROR, /**< 初始化、转换或读取失败，详见adc_status。 */
    APP_MEASUREMENT_REFERENCE_ERROR, /**< 参考为零或饱和，不计算电压。 */
    APP_MEASUREMENT_OUT_OF_RANGE, /**< 信号饱和或换算超出0~5V及边界容差。 */
    APP_MEASUREMENT_STALE, /**< 最后有效测量超过300ms未更新。 */
    APP_MEASUREMENT_ARGUMENT_ERROR /**< 快照输出指针为空。 */
} AppMeasurementStatus;

/** @brief 原子读取的测量快照；非OK时不得使用millivolts作为有效测量。 */
typedef struct {
    uint16_t signal_raw; /**< 完成32组时的测量通道四舍五入平均码；ADC失败置零。 */ // 保存测量通道平均原始码。
    uint16_t reference_raw; /**< 完成32组时的参考通道四舍五入平均码；ADC失败置零。 */ // 保存参考通道平均原始码。
    uint16_t millivolts; /**< 有效输出，单位mV，范围0~5000；非OK时无效。 */ // 保存显示及通信使用的输入电压。
    int32_t unclamped_millivolts; /**< 由累加值比值换算的未限幅输入电压，单位mV。 */ // 保存边界处理前的电压。
    bool boundary_clamped; /**< 仅在±30mV边界容差内归到0或5000mV时为true。 */ // 标记输出是否进行了边界归并。
    uint32_t timestamp_ms; /**< 最近一次测量尝试完成时间，单位ms，自然回绕。 */ // 保存快照更新时刻。
    AppMeasurementStatus status; /**< 结果有效性及失效原因。 */ // 保存快照业务状态。
    DevStatus adc_status; /**< 最近一次初始化或成对读取的Device结果。 */ // 保存底层失败原因。
} AppMeasurementSnapshot;

/** @brief 最近一次测量尝试状态；实时过期检查由GetLatest完成。 */
extern volatile AppMeasurementStatus g_measurement_status;
/** @brief 最近一次ADC操作结果，不表示硬件已经通过实测。 */
extern volatile DevStatus g_measurement_adc_status;
/** @brief 已完成的周期测量尝试次数，包含失败，计数自然回绕。 */
extern volatile uint32_t g_measurement_update_count;
/** @brief 运行期ADC失败周期累计次数，成功后保留，便于排查短暂E02。 */
extern volatile uint32_t g_measurement_adc_error_count;
/** @brief 最近一次ADC失败原因；仅在adc_error_count非零时表示已发生的错误。 */
extern volatile DevStatus g_measurement_last_adc_error;
/** @brief 最近成功快照，仅供调试；可能过期，业务必须调用GetLatest检查实时有效性。 */
extern volatile AppMeasurementSnapshot g_measurement_last_valid;

/**
 * @brief 初始化ADC接口与测量快照，不创建任务；仅在调度器启动前调用。
 * @return 成功APP_OK，ADC初始化失败或重复运行期初始化返回APP_ERROR_MEASUREMENT。
 */
AppStatus App_Measurement_Init(void);
/**
 * @brief 创建静态测量任务及100ms通知定时器；首次采样在调度开始约100ms后。
 * @return 成功APP_OK，未就绪APP_ERROR_MEASUREMENT，任务失败APP_ERROR_TASK，定时器失败APP_ERROR_TIMER。
 */
AppStatus App_Measurement_Start(void);
/**
 * @brief 原子读取快照并检查新鲜度，不启动ADC、不阻塞，不供中断调用。
 * @param output 非空快照输出指针；非OK也返回状态及诊断字段，空指针不写输出。
 * @return 快照状态；只有APP_MEASUREMENT_OK表示电压可使用，空指针返回APP_MEASUREMENT_ARGUMENT_ERROR。
 */
AppMeasurementStatus App_Measurement_GetLatest(AppMeasurementSnapshot *output);
#endif
