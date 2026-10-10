#ifndef APP_FLAME_DETECTION_H
#define APP_FLAME_DETECTION_H

#include "app_flame_protocol.h"

/* 名义盲锁时间加10%及5ms裕量，覆盖手册振荡器范围及调用耗时；仍需实测。 */
enum {
    APP_FLAME_BLIND_STEP_MS = 500, APP_FLAME_BLIND_MARGIN_DIVISOR = 10,
    APP_FLAME_BLIND_IO_MARGIN_MS = 5, APP_FLAME_CAPTURE_TIMEOUT_MS = 100,
    APP_FLAME_MONITOR_INTERVAL_MS = 10, APP_FLAME_BLIND_CODE_MAX = 15,
    APP_FLAME_CHANNEL_ID_BASE = 1
};

/* 仅启动初始化使用：上限只计首次尝试之外的重试，间隔单位为毫秒。 */
enum {
    APP_FLAME_CONFIG_MAX_RETRIES = 5,
    APP_FLAME_CONFIG_RETRY_INTERVAL_MS = 1000
};

/** @brief 固件入口提供的启动计时能力，APP不直接依赖板级实现。 */
typedef struct {
    /** @brief 读取允许自然回绕的实际开机毫秒数。
     * @return 当前毫秒计数。 */
    uint32_t (*get_ms)(void);
    /** @brief 在初始化阶段等待，不执行正常检测或通信轮询。
     * @param interval_ms 至少需要等待的毫秒数。
     * @return 完成等待返回true，计时停止或不能等待返回false。 */
    bool (*wait_ms)(uint32_t interval_ms);
} AppFlameInitTiming;

/** @brief 本路已由用户或实测提供的完整配置；缺失时不操作探头。 */
typedef struct {
    bool supplied;             // 是否确实提供过完整检测配置。
    DevFlameConfig detector;   // 灵敏度、脉冲数等实测参数，禁止隐含阈值。
} AppFlameSetup;

/** @brief 一路应用状态，事件锁存与当前故障彼此独立。 */
typedef struct {
    DevFlame *device;          // GetFlame取得的本路静态接口，非NULL不表示硬件已初始化。
    bool initialized;         // 本路设备配置回读校验成功且未因硬错误失效；检测可用还需检查盲锁等状态。
    bool triggered;           // 有效探头事件锁存，启动或有效清除命令后清零。
    uint32_t minute_event_count; // 本分钟观察到的新事件次数，只在事件入口增加并饱和。
    bool minute_incomplete;    // 本分钟曾未初始化、硬故障或人工清零，到下一分钟重建。
    bool has_sample;          // 是否保存过事件后的完整数据。
    bool blind;               // 是否处于保守盲锁屏蔽期。
    bool awaiting_sample;     // 事件已锁存，等待暂时忙的读取完成。
    bool fault;               // 当前存在配置、查询或读取的硬错误。
    DevFlameResult result;    // 最近一次设备操作结果。
    DevFlameSample sample;    // 历史事件数据，不因读数失败而清空。
    uint32_t sample_ms;       // 最近成功读取事件数据的毫秒时刻。
    uint32_t blind_start_ms;  // 最近一次读数复位或初始化的毫秒时刻。
    uint32_t blind_ms;        // 配置名义盲锁时间加保守裕量。
    uint32_t capture_start_ms; // 事件已观察到但尚未完成读数的起点。
    uint32_t sample_age_ms;   // 饱和累计的历史数据年龄。
    uint32_t age_tick_ms;     // 上次累计年龄的时刻，支持时间回绕。
    DevFlameConfig retry_config; // 最近请求的完整配置副本，启动重试复用同一参数。
    uint32_t retries_done;    // 启动本路已执行的额外重试次数，运行期显式配置重置为0。
    uint32_t last_config_attempt_ms; // 最近一次实际配置尝试开始时的毫秒时刻，仅供诊断。
    bool config_requested;    // 是否收到过本轮显式配置请求，区分缺配置和配置失败。
    DevFlameResult config_result; // 最近一次配置初始化结果，运行期查询、读取及清除不覆盖。
} AppFlameChannelState;

/** @brief 三路检测上下文；由应用串行调用。 */
typedef struct {
    AppFlameChannelState channels[DEV_FLAME_CHANNEL_COUNT]; // U22、U23、U24应用状态。
    uint32_t minute_index;     // 从APP初始化起计的分钟编号，首分钟为0。
    uint32_t minute_elapsed_ms; // 本分钟已过毫秒数，保持小于60000。
    uint32_t minute_tick_ms;   // 最近一次推进分钟时间的毫秒时刻，用于回绕安全差值。
} AppFlameDetection;

/** @brief 建立首分钟及三路应用状态，显式初始化已提供配置的设备并回读核对。
 * @details 清零分钟次数及历史数据；逐路首次尝试及最多5次额外重试全部在返回前完成。
 *          每次重试至少间隔1秒；成功或参数错误立即停止，无配置通道不操作探头。
 * @param detection 非空三路上下文，仅在应用启动调用。
 * @param setup 三路配置数组；NULL或本路supplied为false时，该路保持未就绪且不操作探头。
 * @param now_ms 首分钟起点的毫秒时刻。
 * @param timing 有效取时与等待回调，缺失时已提供配置的通道记录参数错误且不操作探头。 */
void AppFlameDetection_Init(AppFlameDetection *detection, const AppFlameSetup *setup, uint32_t now_ms,
                            const AppFlameInitTiming *timing);
/** @brief 显式初始化本路设备，写入事件配置并回读校验；保留本分钟次数及历史数据。
 * @details 每次请求只尝试一次并重置额外重试计数，失败直接结束，不由轮询重新配置。
 *          成功后进入保守盲锁期；获取设备接口本身不执行初始化或配置写入。
 * @param detection 三路上下文。
 * @param channel 有效Device通道枚举。
 * @param config 用户提供的完整事件检测配置。
 * @param now_ms 当前毫秒时刻。
 * @return 本次初始化及回读结果，无效配置返回ARGUMENT_ERROR。 */
DevFlameResult AppFlameDetection_Configure(AppFlameDetection *detection, DevFlameChannel channel,
                                         const DevFlameConfig *config, uint32_t now_ms);
/** @brief 先推进分钟窗口再统计新事件并消费读数；初始化、强制读数及等待读取不增加次数。
 * @param detection 三路上下文。
 * @param now_ms 当前毫秒时刻。
 * @return 换分钟或事件次数、故障、检测可用性变化时返回true，不执行配置重试。 */
bool AppFlameDetection_Poll(AppFlameDetection *detection, uint32_t now_ms);
/** @brief 清除成功通道的探头事件、分钟次数及历史，保持分钟计时并固定保存逐路结果。
 * @param detection 三路检测上下文。
 * @param now_ms 命令生效时的开机毫秒数。
 * @param report 非空结果输出，保留调用方填写的地址、设备序号及命令序号；失败通道保留锁存。 */
void AppFlameDetection_Clear(AppFlameDetection *detection, uint32_t now_ms, AppFlameClearReport *report);
/** @brief 生成三路状态快照及汇总结果。
 * @param detection 三路上下文。
 * @param now_ms 当前毫秒时刻。
 * @param report 接收报告的非空输出对象，保留头部序号和类型。 */
void AppFlameDetection_Snapshot(AppFlameDetection *detection, uint32_t now_ms, AppFlameReport *report);

/** @brief 推进固定分钟窗口并生成三路次数及诊断快照，保留报告类型、地址与序号。
 * @param detection 非空检测上下文。
 * @param now_ms 当前毫秒时刻，允许自然回绕。
 * @param report 非空分钟计数输出对象。
 * @return 换分钟或首次标记本分钟统计不完整时返回true，供APP申请变化上报。 */
bool AppFlameDetection_MinuteSnapshot(AppFlameDetection *detection, uint32_t now_ms,
                                    AppFlameMinuteReport *report);

/** @brief 生成独立初始化快照，保留最近配置结果并报告本轮重试次数。
 * @param detection 非空三路应用上下文。
 * @param now_ms 打包时开机毫秒时间。
 * @param report 非空输出快照，地址和序号由上报模块维护。 */
void AppFlameDetection_InitSnapshot(const AppFlameDetection *detection, uint32_t now_ms, AppFlameInitReport *report);

#endif
