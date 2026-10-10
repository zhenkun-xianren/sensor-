#ifndef APP_FLAME_PROTOCOL_H
#define APP_FLAME_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>
#include "dev_flame.h"

/* 历史检测65字节、分钟计数80字节、清除命令11字节、初始化47字节、清除结果26字节。 */
enum {
    APP_FLAME_FRAME_BYTES = 65, APP_FLAME_PAYLOAD_BYTES = 50,
    APP_FLAME_HEADER_FIRST = 0xAA, APP_FLAME_HEADER_SECOND = 0x55,
    APP_FLAME_PROTOCOL_VERSION = 1, APP_FLAME_DEVICE_ADDRESS = 1,
    APP_FLAME_TYPE_PERIODIC = 1, APP_FLAME_TYPE_CHANGE = 2,
    APP_FLAME_TYPE_CLEAR_COMMAND = 3,
    APP_FLAME_TYPE_INITIALIZATION = 4,
    APP_FLAME_TYPE_CLEAR_RESULT = 5,
    APP_FLAME_TYPE_MINUTE_PERIODIC = 6, APP_FLAME_TYPE_MINUTE_CHANGE = 7,
    APP_FLAME_METHOD_INTERNAL = 1, APP_FLAME_CHANNEL_BYTES = 16,
    APP_FLAME_VERSION_OFFSET = 2, APP_FLAME_TYPE_OFFSET = 3,
    APP_FLAME_ADDRESS_OFFSET = 4, APP_FLAME_SEQUENCE_OFFSET = 5,
    APP_FLAME_LENGTH_OFFSET = 7, APP_FLAME_UPTIME_OFFSET = 9,
    APP_FLAME_DECISION_OFFSET = 13, APP_FLAME_METHOD_OFFSET = 14,
    APP_FLAME_CHANNELS_OFFSET = 15, APP_FLAME_CRC_OFFSET = 63,
    APP_FLAME_CHANNEL_ID_OFFSET = 0, APP_FLAME_CHANNEL_DECISION_OFFSET = 1,
    APP_FLAME_FLAGS_OFFSET = 2, APP_FLAME_RESULT_OFFSET = 4,
    APP_FLAME_ADC_OFFSET = 6, APP_FLAME_CONFIG_OFFSET = 8,
    APP_FLAME_AGE_OFFSET = 12, APP_FLAME_BYTE_BITS = 8,
    APP_FLAME_WORD_BYTES = 2, APP_FLAME_DWORD_BYTES = 4,
    APP_FLAME_CRC_INITIAL = 0xFFFF, APP_FLAME_CRC_POLYNOMIAL = 0xA001,
    APP_FLAME_CRC_LOW_BIT = 1, APP_FLAME_CRC_SHIFT = 1,
    APP_FLAME_ADC_MAX = 0x3FFF, APP_FLAME_HEADER_FIRST_OFFSET = 0,
    APP_FLAME_HEADER_SECOND_OFFSET = 1, APP_FLAME_CHANNEL_ID_FIRST = 1,
    APP_FLAME_KNOWN_FLAGS = 0x003F, APP_FLAME_SIGN_BIT = 0x8000,
    APP_FLAME_CLEAR_FRAME_BYTES = 11, APP_FLAME_CLEAR_CRC_OFFSET = 9,
    APP_FLAME_CLEAR_PAYLOAD_BYTES = 0
};

/* 固定分钟计数帧：载荷不含开机时间，通道字段逐字节编码，不依赖结构体对齐。 */
enum {
    APP_FLAME_MINUTE_PERIOD_MS = 60000,
    APP_FLAME_MINUTE_FRAME_BYTES = 80, APP_FLAME_MINUTE_PAYLOAD_BYTES = 65,
    APP_FLAME_MINUTE_INDEX_OFFSET = 13, APP_FLAME_MINUTE_ELAPSED_OFFSET = 17,
    APP_FLAME_MINUTE_CHANNELS_OFFSET = 21, APP_FLAME_MINUTE_CHANNEL_BYTES = 19,
    APP_FLAME_MINUTE_FLAGS_OFFSET = 1, APP_FLAME_MINUTE_RESULT_OFFSET = 3,
    APP_FLAME_MINUTE_ADC_OFFSET = 5, APP_FLAME_MINUTE_CONFIG_OFFSET = 7,
    APP_FLAME_MINUTE_AGE_OFFSET = 11, APP_FLAME_MINUTE_COUNT_OFFSET = 15,
    APP_FLAME_MINUTE_CRC_OFFSET = 78, APP_FLAME_MINUTE_KNOWN_FLAGS = 0x007F
};
#define APP_FLAME_MINUTE_COUNT_MAX UINT32_MAX /* 单路次数饱和上限，禁止溢出回零。 */

/* 初始化状态帧沿用版本及通用帧头；载荷在开机时间之后，CRC覆盖此前所有字节。 */
enum {
    APP_FLAME_INIT_FRAME_BYTES = 47, APP_FLAME_INIT_PAYLOAD_BYTES = 32,
    APP_FLAME_INIT_RETRY_LIMIT_OFFSET = 13, APP_FLAME_INIT_RETRY_INTERVAL_OFFSET = 17,
    APP_FLAME_INIT_CHANNELS_OFFSET = 21, APP_FLAME_INIT_CHANNEL_BYTES = 8,
    APP_FLAME_INIT_STATUS_OFFSET = 1, APP_FLAME_INIT_RESULT_OFFSET = 2,
    APP_FLAME_INIT_RETRIES_OFFSET = 4, APP_FLAME_INIT_CRC_OFFSET = 45
};

/* 清除结果的两个位图按bit0/1/2对应U22/U23/U24，结果码为16位补码。 */
enum {
    APP_FLAME_CLEAR_RESULT_FRAME_BYTES = 26, APP_FLAME_CLEAR_RESULT_PAYLOAD_BYTES = 11,
    APP_FLAME_CLEAR_REQUEST_SEQUENCE_OFFSET = 13, APP_FLAME_CLEAR_SUCCESS_OFFSET = 15,
    APP_FLAME_CLEAR_NOT_INITIALIZED_OFFSET = 16, APP_FLAME_CLEAR_FAILED_OFFSET = 17,
    APP_FLAME_CLEAR_RESULTS_OFFSET = 18, APP_FLAME_CLEAR_RESULT_CRC_OFFSET = 24,
    APP_FLAME_CLEAR_CHANNEL_MASK = 0x07, APP_FLAME_CLEAR_CHANNEL_BIT = 1,
    APP_FLAME_CLEAR_NO_CHANNELS = 0,
    APP_FLAME_CLEAR_FAILURE = 0, APP_FLAME_CLEAR_SUCCESS = 1
};
#define APP_FLAME_CONFIG_MAX 0x01FFFFFFUL /* 25位配置域上限。 */
#define APP_FLAME_WORD_MODULUS 0x10000L /* 16位补码转换使用的模数。 */
#define APP_FLAME_EMPTY_FLAGS 0U /* 无状态位置位。 */
#define APP_FLAME_EMPTY_RAW 0U /* 无事件数据时的占位值，必须结合HAS_SAMPLE读取。 */
#define APP_FLAME_NO_SAMPLE_AGE UINT32_MAX /* 尚无事件原始数据。 */
#define APP_FLAME_MAX_SAMPLE_AGE (UINT32_MAX - 1UL) /* 保留无数据哨兵。 */

/** @brief 内置事件的检测结果；未检出不保证物理上绝对没有火焰。 */
typedef enum {
    APP_FLAME_NOT_DETECTED = 0, /**< 尚未记录有效事件。 */
    APP_FLAME_TRIGGERED = 1,    /**< 有效事件已锁存，重启或有效清除命令后清零。 */
    APP_FLAME_UNKNOWN = 2       /**< 检测当前不可用或处于盲锁期。 */
} AppFlameDecision;

/** @brief 通道状态位，各位可同时成立。 */
typedef enum {
    APP_FLAME_FLAG_DETECTION_READY = 1U << 0, /**< 当前检测可用。 */
    APP_FLAME_FLAG_HAS_SAMPLE = 1U << 1,      /**< 有历史事件原始数据。 */
    APP_FLAME_FLAG_SAMPLE_VALID = 1U << 2,    /**< 历史数据完整且未超量程。 */
    APP_FLAME_FLAG_OVER_RANGE = 1U << 3,      /**< 历史数据超量程。 */
    APP_FLAME_FLAG_FAULT = 1U << 4,           /**< 当前有配置或驱动故障。 */
    APP_FLAME_FLAG_BLIND = 1U << 5,           /**< 事件复位后的保守盲锁期。 */
    APP_FLAME_FLAG_MINUTE_INCOMPLETE = 1U << 6 /**< 仅分钟帧：本分钟曾未初始化、硬故障或人工清零。 */
} AppFlameFlags;

/** @brief 一路本分钟事件次数与诊断数据；一次内置事件不等于一个物理火焰。 */
typedef struct {
    uint8_t channel_id;         // 线上通道号1至3。
    uint16_t flags;             // 当前诊断状态及本分钟统计不完整标志。
    DevFlameResult result;      // 最近设备操作结果。
    uint16_t adc_raw;           // 最近事件的14位原始码，无数据时占位。
    uint32_t config_raw;        // 最近事件的配置回读，无数据时占位。
    uint32_t sample_age_ms;     // 历史数据年龄，无数据时为哨兵。
    uint32_t event_count;       // 本分钟观察到的事件次数，人工清除成功后清零。
} AppFlameMinuteChannelReport;

/** @brief 固定60秒窗口的三路计数快照，不含是否触发的锁存结果。 */
typedef struct {
    uint8_t type;               // 分钟周期或变化报告类型。
    uint8_t address;            // 设备线上地址。
    uint16_t sequence;          // 所有报告共用的提交发送序号。
    uint32_t uptime_ms;         // 打包时开机毫秒计数，允许自然回绕。
    uint32_t minute_index;      // 从APP初始化起计的分钟编号，首分钟为0。
    uint32_t elapsed_ms;        // 本分钟已过毫秒数，范围0至59999。
    AppFlameMinuteChannelReport channels[DEV_FLAME_CHANNEL_COUNT]; // 固定U22/U23/U24顺序。
} AppFlameMinuteReport;

/** @brief 将固定分钟计数及诊断数据编码为80字节帧。
 * @param report 非空分钟计数快照。
 * @param output 接收完整帧的非空缓冲区。
 * @param capacity 输出缓冲区字节容量，不小于MINUTE_FRAME_BYTES。
 * @return 参数及字段有效且编码完成时返回true。 */
bool AppFlameMinuteFrame_Encode(const AppFlameMinuteReport *report, uint8_t *output, uint16_t capacity);
/** @brief 校验并解码80字节分钟帧；失败时不修改输出。
 * @param input 非空完整帧字节序列。
 * @param length 实际帧长，必须为MINUTE_FRAME_BYTES。
 * @param report 解码成功后接收快照的非空对象。
 * @return 帧头、版本、载荷、CRC及字段均有效时返回true。 */
bool AppFlameMinuteFrame_Decode(const uint8_t *input, uint16_t length, AppFlameMinuteReport *report);

/** @brief 一路协议快照；禁止将结构体内存直接发到串口。 */
typedef struct {
    uint8_t channel_id;          // 线上通道号1至3。
    AppFlameDecision decision;   // 本路事件锁存或未知状态。
    uint16_t flags;              // AppFlameFlags的组合。
    DevFlameResult result;       // 最近初始化、查询或读取的结果。
    uint16_t adc_raw;            // 14位原始位码，不擅自解释符号。
    uint32_t config_raw;         // 与原始数据对应的25位配置回读。
    uint32_t sample_age_ms;      // 历史数据年龄，UINT32_MAX表示无数据。
} AppFlameChannelReport;

/** @brief 三路完整状态帧的逻辑内容。 */
typedef struct {
    uint8_t type;                // 周期或状态变化消息类型。
    uint8_t address;             // 发起上报的设备地址。
    uint16_t sequence;           // 本次提交发送的帧序号。
    uint32_t uptime_ms;          // 打包时的开机毫秒计数，可自然回绕。
    AppFlameDecision decision;   // 三路汇总检测结果。
    AppFlameChannelReport channels[DEV_FLAME_CHANNEL_COUNT]; // 固定U22/U23/U24顺序。
} AppFlameReport;

/** @brief 最近一轮配置初始化的结果，成功不等于当前已退出盲锁或没有运行期故障。 */
typedef enum {
    APP_FLAME_INIT_NOT_CONFIGURED = 0, /**< 没有收到本路配置，不执行初始化。 */
    APP_FLAME_INIT_SUCCESS = 1,        /**< 最近一次配置初始化及回读核对成功。 */
    APP_FLAME_INIT_RETRYING = 2,       /**< 最近尝试失败，仍有待执行的有限重试。 */
    APP_FLAME_INIT_FAILED = 3          /**< 本轮失败且停止重试，或参数、接口无效。 */
} AppFlameInitStatus;

/** @brief 一路初始化报告，重试次数不包含首次尝试。 */
typedef struct {
    uint8_t channel_id;              // 线上通道号1至3。
    AppFlameInitStatus status;       // 本轮初始化成功、重试中、失败或未配置。
    DevFlameResult result;           // 最近一次配置初始化结果，不被查询或采样结果覆盖。
    uint32_t retries_done;           // 本轮已执行的额外重试次数。
} AppFlameInitChannelReport;

/** @brief 独立初始化状态帧内容，与检测状态帧共用发送序号。 */
typedef struct {
    uint8_t address;                 // 设备线上地址。
    uint16_t sequence;               // 两种上报帧共用的提交发送序号。
    uint32_t uptime_ms;              // 打包时开机毫秒计数。
    uint32_t retry_limit;            // 每路允许的额外重试次数，不包含首次尝试。
    uint32_t retry_interval_ms;      // 两次配置尝试之间的最小间隔，单位毫秒。
    AppFlameInitChannelReport channels[DEV_FLAME_CHANNEL_COUNT]; // 固定U22/U23/U24顺序。
} AppFlameInitReport;

/** @brief 一次清除命令的固定结果；后续检测及初始化重试不覆盖该快照。 */
typedef struct {
    uint8_t address;                 // 设备线上地址。
    uint16_t sequence;               // 与检测、初始化报告共用的设备发送序号。
    uint32_t uptime_ms;              // 执行本次清除时的开机毫秒数。
    uint16_t request_sequence;       // 原始清除命令的序号，用于关联请求。
    bool success;                   // 三路实际清除结果均为OK时才为true。
    uint8_t not_initialized_mask;    // 清除时未成功初始化的通道位图。
    uint8_t failed_mask;             // 已尝试清除但操作未成功的通道位图。
    DevFlameResult results[DEV_FLAME_CHANNEL_COUNT]; // 固定U22/U23/U24顺序的本次结果码。
} AppFlameClearReport;

/** @brief 将本次清除结果编码为26字节独立应答，校验总体结果、位图与各路结果的一致性。
 * @param report 本次清除命令的固定结果快照。
 * @param output 接收完整帧的非空缓冲区。
 * @param capacity 缓冲区字节数，不小于CLEAR_RESULT_FRAME_BYTES。
 * @return 字段有效且编码成功时返回true，否则返回false。 */
bool AppFlameClearFrame_Encode(const AppFlameClearReport *report, uint8_t *output, uint16_t capacity);

/** @brief 将三路初始化状态及重试次数编码为47字节独立帧。
 * @param report 非空初始化状态快照。
 * @param output 接收编码数据的非空缓冲区。
 * @param capacity 缓冲区容量，不小于INIT_FRAME_BYTES。
 * @return 参数和通道字段有效且编码成功时返回true。 */
bool AppFlameInitFrame_Encode(const AppFlameInitReport *report, uint8_t *output, uint16_t capacity);

/** @brief 按固定偏移组装65字节帧并计算CRC。
 * @param report 三路一致快照。
 * @param output 接收编码字节的非空缓冲区。
 * @param capacity 缓冲区字节容量，不小于FRAME_BYTES。
 * @return 参数及逻辑内容有效且编码完成返回true。 */
bool AppFlameFrame_Encode(const AppFlameReport *report, uint8_t *output, uint16_t capacity);
/** @brief 校验并解码完整V1帧；失败不修改报告对象。
 * @param input 已接收的完整字节序列。
 * @param length 字节数，必须为FRAME_BYTES。
 * @param report 成功时接收解码结果。
 * @return 帧头、版本、长度、CRC和关键字段全部有效返回true。 */
bool AppFlameFrame_Decode(const uint8_t *input, uint16_t length, AppFlameReport *report);

/** @brief 只接受本机地址、空载荷、完整CRC的清除三路锁存命令。
 * @param input 已接收的原始帧。
 * @param length 实际字节数，必须为CLEAR_FRAME_BYTES。
 * @param address 本机设备地址。
 * @return 命令格式与校验全部正确时返回true。 */
bool AppFlameCommand_IsClear(const uint8_t *input, uint16_t length, uint8_t address);

#endif
