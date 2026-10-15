#ifndef APP_ULTRASONIC_RANGE_H
#define APP_ULTRASONIC_RANGE_H

#include "dev_ultrasonic_range.h"

/* 第1路先发后收测反射距离：4～60ms开门，100us保护及首次200us静默后检查连续3个下降沿。
 * 按名义1.65V基准线比较，连续同极性沿间隔20～30us；保留原滤波及其他通道关闭配置。
 * 距离按假定声速及往返时间除以2换算，不计算风速。 */
#define APP_ULTRASONIC_RANGE_HOLD_CLAMP_TEST false
/* false为第1路先发射再延时接收；true为持续只接收。 */
#define APP_ULTRASONIC_RANGE_RECEIVE_ONLY_TEST false

/* 可调测试参数：2m为有效距离上限，超限丢弃；不代表本板已验证的性能。 */
#define APP_ULTRASONIC_RANGE_BURST_CYCLES 8U
/* 名义VDDA=3.3V，2048/4095*3.3约1.6504V；比较约1.65V基准线，不自动跟踪。 */
#define APP_ULTRASONIC_RANGE_THRESHOLD_CODE 2048U
/* 从发射起点4ms开门，保留100us保护；多沿模式首次静默200us，无干扰时约4.2ms可取首候选。 */
#define APP_ULTRASONIC_RANGE_BLANKING_US 4000UL
/* 截止保持60ms；换算后仅接受不超过2000mm的距离，不因窗口更长而放宽上限。 */
#define APP_ULTRASONIC_RANGE_TIMEOUT_US 60000UL
/* 近端候选被忽略后继续等待远端回波，避免仅屏蔽最终显示值。 */
#define APP_ULTRASONIC_RANGE_MIN_DISTANCE_MM 500UL
#define APP_ULTRASONIC_RANGE_MAX_DISTANCE_MM 2000UL
#define APP_ULTRASONIC_RANGE_SOUND_SPEED_MM_S 343000UL
#define APP_ULTRASONIC_RANGE_TIME_OFFSET_US 0L
#define APP_ULTRASONIC_RANGE_PERIOD_MS 100U
#define APP_ULTRASONIC_RANGE_QUERY_MS 1U
#define APP_ULTRASONIC_RANGE_WATCHDOG_MARGIN_MS 10U
#define APP_ULTRASONIC_RANGE_US_PER_MS 1000UL
#define APP_ULTRASONIC_RANGE_STACK_WORDS 256U
#define APP_ULTRASONIC_RANGE_TASK_PRIORITY (tskIDLE_PRIORITY + 1U)
#define APP_ULTRASONIC_RANGE_COMMAND_WAIT_TICKS 0U

/* 每轮合格毫米距离的循环历史，满后覆盖最旧项；容量为128项。 */
#define APP_ULTRASONIC_RANGE_VALID_BUFFER_CAPACITY 128UL

/* 稳定输出只确认多轮一致的数据，不改变原始测距变量。 */
#define APP_ULTRASONIC_RANGE_FILTER_SAMPLES 3U
#define APP_ULTRASONIC_RANGE_FILTER_MEDIAN_INDEX (APP_ULTRASONIC_RANGE_FILTER_SAMPLES / 2U)
#define APP_ULTRASONIC_RANGE_FILTER_MIN_TOLERANCE_MM 100UL
#define APP_ULTRASONIC_RANGE_FILTER_RELATIVE_PERCENT 10UL
#define APP_ULTRASONIC_RANGE_FILTER_PERCENT_SCALE 100UL
#define APP_ULTRASONIC_RANGE_FILTER_EXPIRE_MS 500U

/** @brief 多轮校验后的距离；valid为真才可使用，stale为真表示暂时保留旧结果。 */
typedef struct {
    uint32_t distance_mm; /**< 三点中值距离，单位mm；未确认、停止或过期时为0。 */
    uint32_t age_ms; /**< 距最近一次可信更新的时间，单位ms；达到500ms时失效。 */
    uint32_t sequence; /**< 最近一次可信更新对应的原始测量序号；不是当前进行中序号。 */
    uint32_t pending_samples; /**< 当前一致候选样本数，范围0至3；3表示已经确认。 */
    uint32_t accepted_updates; /**< 自上电累计可信更新次数，停止/重启保留，自然回绕。 */
    uint32_t outlier_count; /**< 相邻有效距离超出一致性容差的累计次数，包含待重新确认的真实跳变。 */
    uint32_t invalid_measurements; /**< 原始完成轮次中valid为假的累计次数，进行中不计入。 */
    bool valid; /**< 真表示存在未过期的稳定距离；不证明捕获信号一定来自目标直接回波。 */
    bool stale; /**< 真表示旧结果被暂时保留或已过期；结合valid和age_ms判断新旧。 */
} AppUltrasonicRangeFilteredResult;

/** @brief 稳定测距输出，原始g_ultrasonic_range_result及last_result保持原有语义。 */
extern volatile AppUltrasonicRangeFilteredResult g_ultrasonic_range_filtered_result;

/** @brief 调试器实时结果；测距结合valid和sequence查看距离，只接收时距离保持无效。 */
extern volatile DevUltrasonicRangeResult g_ultrasonic_range_result;

/** @brief 最近完成或失败的一轮结果；完成计数非零时可查看，sequence标识历史轮次。
 * 新测量、Stop和Start保留它；valid仅说明该历史轮次的距离有效，不代表当前还在测量。 */
extern volatile DevUltrasonicRangeResult g_ultrasonic_range_last_result;
/** @brief 自上电累计完成/失败次数，自然回绕；人为停止和进行中不计入。 */
extern volatile uint32_t g_ultrasonic_range_completed_count;
/** @brief 自上电累计valid为真的完成次数，自然回绕。 */
extern volatile uint32_t g_ultrasonic_range_valid_count;
/** @brief 只读Watch循环历史；仅记录新完成轮次的合格distance_mm，单位mm，写满覆盖最旧项。 */
extern volatile uint32_t g_ultrasonic_range_valid_buffer[APP_ULTRASONIC_RANGE_VALID_BUFFER_CAPACITY];
/** @brief 下一写入索引，范围0至容量减1；缓冲区满时也指向最旧项，停止/重启测量保留。 */
extern volatile uint32_t g_ultrasonic_range_valid_buffer_next_index;
/** @brief 缓冲区有效项数，0至容量；满后保持容量，MCU复位后清零。 */
extern volatile uint32_t g_ultrasonic_range_valid_buffer_count;
/** @brief 自上电累计无合格回波的截止次数，自然回绕。 */
extern volatile uint32_t g_ultrasonic_range_no_echo_count;

/** @brief 显式初始化设备；钳位测试仅发布初始结果，正常模式建立查询任务及所需周期通知。
 * @return 成功DEV_OK，资源建立失败DEV_IO_ERROR，其他返回设备原错误。 */
DevStatus APP_UltrasonicRange_Start(void);

/** @brief 停止周期触发和设备，清除有效距离；已有静态对象保留供重启。
 * @return 成功DEV_OK，未建立设备DEV_NOT_READY，定时器命令或设备关闭失败DEV_IO_ERROR。 */
DevStatus APP_UltrasonicRange_Stop(void);

#endif
