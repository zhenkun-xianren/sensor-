#include <stddef.h>
#include <string.h>
#include "app_flame_detection.h"

/** @brief 按时间差直接推进固定分钟窗口，跨多个窗口也不执行等待或补算循环。
 * @param detection 已建立的三路上下文。
 * @param now_ms 当前毫秒时刻，调用间隔须小于一个32位时基周期。
 * @return 至少进入一个新分钟时返回true。 */
static bool app_flame_advance_minute(AppFlameDetection *detection, uint32_t now_ms)
{
    uint32_t delta = now_ms - detection->minute_tick_ms; // 自最近推进以来的回绕安全差值。
    uint64_t elapsed = (uint64_t)detection->minute_elapsed_ms + delta; // 扩宽后相加避免32位溢出。
    uint32_t windows = (uint32_t)(elapsed / APP_FLAME_MINUTE_PERIOD_MS); // 已跨过的完整窗口数。
    uint8_t index; // 需要重建计数的通道索引。
    detection->minute_tick_ms = now_ms;
    detection->minute_elapsed_ms = (uint32_t)(elapsed % APP_FLAME_MINUTE_PERIOD_MS);
    if (windows == 0) {
        return false;
    }
    detection->minute_index += windows;
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        AppFlameChannelState *state = &detection->channels[index]; // 当前通道的新分钟状态。
        state->minute_event_count = 0;
        state->minute_incomplete = !state->initialized || state->fault;
    }
    return true;
}

/** @brief 累计历史数据年龄，支持毫秒时基回绕并保留哨兵。
 * @param state 本路应用状态。
 * @param now_ms 当前时间。 */
static void app_flame_update_age(AppFlameChannelState *state, uint32_t now_ms)
{
    uint32_t delta = now_ms - state->age_tick_ms; // 自上次累计以来的回绕安全时间差。
    state->age_tick_ms = now_ms;
    if (state->has_sample) {
        state->sample_age_ms = (delta >= APP_FLAME_MAX_SAMPLE_AGE - state->sample_age_ms) ?
            APP_FLAME_MAX_SAMPLE_AGE : state->sample_age_ms + delta;
    }
}

/** @brief 更新操作结果，暂时忙或等待不判为硬故障。
 * @param state 本路状态。
 * @param result 最近一次Device结果。
 * @return 结果或故障标志变化返回true。 */
static bool app_flame_set_result(AppFlameChannelState *state, DevFlameResult result)
{
    bool hard_error = (result != DEV_FLAME_OK) && (result != DEV_FLAME_PENDING) &&
                      (result != DEV_FLAME_BUSY); // 只有硬错误使检测失效。
    bool changed = (state->result != result) || (state->fault != hard_error); // 本次是否应提前上报。
    state->result = result;
    state->fault = hard_error;
    if (hard_error) {
        state->minute_incomplete = true; // 本分钟故障痕迹不被后续成功结果覆盖。
        state->initialized = false;
        state->awaiting_sample = false;
        state->blind = false;
    }
    return changed;
}

/** @brief 使用配置副本执行一次初始化与回读，仅保存本次结果。
 * @details 保留历史事件与数据；成功进入盲锁期，不安排自动重试。
 * @param state 本路应用状态，配置副本已由调用方建立。
 * @param now_ms 当前时间。
 * @return 本次实际初始化及回读结果，接口无效返回参数错误。 */
static DevFlameResult app_flame_try_config(AppFlameChannelState *state, uint32_t now_ms)
{
    const DevFlameConfig *config = &state->retry_config; // 本轮复制的配置，首次与重试使用相同参数。
    DevFlameResult result; // 完整配置初始化及回读结果。
    uint32_t nominal_blind; // 配置给出的名义盲锁毫秒数。
    app_flame_update_age(state, now_ms);
    if ((state->device == NULL) || (state->device->init == NULL)) {
        state->config_result = DEV_FLAME_ARGUMENT_ERROR;
        (void)app_flame_set_result(state, DEV_FLAME_ARGUMENT_ERROR);
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    state->initialized = false;
    state->minute_incomplete = true; // 本轮配置期间无法监视事件，成功后也保留本分钟的未初始化痕迹。
    state->awaiting_sample = false;
    state->blind = false;
    state->last_config_attempt_ms = now_ms;
    result = state->device->init(state->device, config); // 显式执行设备初始化、配置写入和回读校验。
    state->config_result = result;
    (void)app_flame_set_result(state, result);
    if (result == DEV_FLAME_OK) {
        nominal_blind = ((uint32_t)config->blind_time_code + APP_FLAME_CHANNEL_ID_BASE) * APP_FLAME_BLIND_STEP_MS;
        state->blind_ms = nominal_blind + nominal_blind / APP_FLAME_BLIND_MARGIN_DIVISOR + APP_FLAME_BLIND_IO_MARGIN_MS;
        state->initialized = true;
        state->awaiting_sample = false;
        state->blind = true; /* 初始化强制回读可能产生复位，保守等待而非当作火焰事件。 */
        state->blind_start_ms = now_ms;
    }
    return result;
}

/** @brief 显式配置本路一次，失败直接结束，不安排轮询重试。
 * @details 复制完整配置并重置额外重试计数，保留本分钟次数与历史数据。
 * @param detection 三路状态。
 * @param channel 设备逻辑通道。
 * @param config 完整的事件模式配置。
 * @param now_ms 当前时间。
 * @return 本次初始化及回读结果或参数错误。 */
DevFlameResult AppFlameDetection_Configure(AppFlameDetection *detection, DevFlameChannel channel,
                                         const DevFlameConfig *config, uint32_t now_ms)
{
    AppFlameChannelState *state; // 当前本路应用对象。
    if ((detection == NULL) || ((unsigned)channel >= DEV_FLAME_CHANNEL_COUNT)) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    (void)app_flame_advance_minute(detection, now_ms);
    state = &detection->channels[channel];
    state->config_requested = true;
    state->retries_done = 0;
    if ((config == NULL) || !config->detection_enabled ||
        (config->interrupt_source != DEV_FLAME_INTERRUPT_EVENT) ||
        ((config->adc_source != DEV_FLAME_SOURCE_BPF) && (config->adc_source != DEV_FLAME_SOURCE_LPF)) ||
        (config->blind_time_code > APP_FLAME_BLIND_CODE_MAX)) {
        state->config_result = DEV_FLAME_ARGUMENT_ERROR;
        (void)app_flame_set_result(state, DEV_FLAME_ARGUMENT_ERROR);
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    state->retry_config = *config;
    if (state->device == NULL) {
        state->device = GetFlame(channel); // 仅补取静态接口，不初始化硬件或写入配置。
    }
    return app_flame_try_config(state, now_ms);
}

/** @brief 建立三路应用状态并获取接口，对已提供配置的通道执行设备初始化与回读校验。
 * @details 仅在应用启动调用，建立首分钟并清除次数与历史数据；逐路配置及重试全部在返回前完成。
 *          逐路最多额外重试5次，每次至少等待1秒；无配置、无效配置或接口不重试。
 * @param detection 三路应用上下文。
 * @param setup 三路配置数组；NULL或本路supplied为false时，该路保持未就绪且不操作探头。
 * @param now_ms 首分钟起点。
 * @param timing 固件入口提供的实际取时与有界等待回调，缺失时不操作已提供配置的通道。 */
void AppFlameDetection_Init(AppFlameDetection *detection, const AppFlameSetup *setup, uint32_t now_ms,
                            const AppFlameInitTiming *timing)
{
    uint8_t index; // 三路通道索引。
    bool timing_valid = (timing != NULL) && (timing->get_ms != NULL) &&
                        (timing->wait_ms != NULL); // 初始化重试所需计时能力是否完整。
    if (detection == NULL) {
        return;
    }
    memset(detection, 0, sizeof(*detection));
    detection->minute_tick_ms = now_ms;
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        AppFlameChannelState *state = &detection->channels[index]; // 本路待建立的应用状态。
        state->device = GetFlame((DevFlameChannel)index); // 仅取得本路静态接口，不初始化硬件或写入配置。
        state->result = DEV_FLAME_NOT_READY; // 已取得接口仍需显式初始化，尚不能判定检测可用。
        state->config_result = DEV_FLAME_NOT_READY;
        state->fault = true;
        state->minute_incomplete = true; // 首分钟从APP初始化起计，设备配置完成前已有未就绪阶段。
        state->age_tick_ms = now_ms;
    }
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        AppFlameChannelState *state = &detection->channels[index]; // 按U22、U23、U24逐路完成启动配置。
        if ((setup != NULL) && setup[index].supplied) {
            DevFlameResult result; // 当前配置尝试结果，决定是否继续本路启动重试。
            uint32_t completed_ms; // 最近一次实际配置完成时刻，等待从此时开始。
            uint32_t attempt_ms; // 下一次实际配置开始时刻，使用回绕安全差值核对间隔。
            if (!timing_valid) {
                state->config_requested = true;
                state->config_result = DEV_FLAME_ARGUMENT_ERROR;
                (void)app_flame_set_result(state, DEV_FLAME_ARGUMENT_ERROR);
                continue;
            }
            result = AppFlameDetection_Configure(detection, (DevFlameChannel)index,
                                                &setup[index].detector, timing->get_ms());
            for (;;) {
                completed_ms = timing->get_ms();
                app_flame_update_age(state, completed_ms);
                if (result == DEV_FLAME_OK) {
                    state->blind_start_ms = completed_ms; // 从完整配置回读结束后开始保守盲锁。
                    break;
                }
                if ((result == DEV_FLAME_ARGUMENT_ERROR) ||
                    (state->retries_done >= APP_FLAME_CONFIG_MAX_RETRIES)) {
                    break;
                }
                if (!timing->wait_ms(APP_FLAME_CONFIG_RETRY_INTERVAL_MS)) {
                    state->config_result = DEV_FLAME_TIMEOUT;
                    (void)app_flame_set_result(state, DEV_FLAME_TIMEOUT);
                    break;
                }
                attempt_ms = timing->get_ms();
                if ((uint32_t)(attempt_ms - completed_ms) < APP_FLAME_CONFIG_RETRY_INTERVAL_MS) {
                    state->config_result = DEV_FLAME_TIMEOUT;
                    (void)app_flame_set_result(state, DEV_FLAME_TIMEOUT);
                    break; // 不把提前返回或停滞的等待算作一次实际配置重试。
                }
                ++state->retries_done;
                result = app_flame_try_config(state, attempt_ms);
            }
        }
    }
    if (timing_valid) {
        (void)app_flame_advance_minute(detection, timing->get_ms());
        for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
            detection->channels[index].minute_incomplete = true; // 启动等待期间尚未开始正常事件监视。
        }
    }
}

/** @brief 消费已保存的事件读数，不执行强制高电平操作。
 * @param state 本路状态，triggered已在调用前置位。
 * @param now_ms 当前时间。
 * @return 状态变化或新数据取得时返回true。 */
static bool app_flame_capture(AppFlameChannelState *state, uint32_t now_ms)
{
    DevFlameSample sample; // 仅成功时由Device填写的数据。
    DevFlameResult result; // 当前事件对应的读取结果。
    bool changed; // 是否有需要提前上报的变化。
    /*检测采样是否超时*/
    if ((uint32_t)(now_ms - state->capture_start_ms) >= APP_FLAME_CAPTURE_TIMEOUT_MS) {
        return app_flame_set_result(state, DEV_FLAME_TIMEOUT);
    }
    /*读取事件数据*/
    result = state->device->read(state->device, DEV_FLAME_READ_READY, &sample);
    changed = app_flame_set_result(state, result);
    if (result == DEV_FLAME_OK) {
        state->sample = sample;
        state->sample_ms = now_ms;
        state->sample_age_ms = 0;
        state->age_tick_ms = now_ms;
        state->has_sample = true;
        state->awaiting_sample = false;
        state->blind = true;
        state->blind_start_ms = now_ms;
        changed = true;
    }
    return changed;
}

/** @brief 先推进分钟窗口再监视新事件，只查询和读取，不重新配置探头。
 * @param detection 三路上下文。
 * @param now_ms 当前时间。
 * @return 换分钟或可用性、故障、次数、原始数据变化时返回true。 */
bool AppFlameDetection_Poll(AppFlameDetection *detection, uint32_t now_ms)
{
    uint8_t index; // 本次遍历的通道索引。
    bool changed = false; // 合并三路变化通知。
    if (detection == NULL) {
        return false;
    }
    changed = app_flame_advance_minute(detection, now_ms);
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        AppFlameChannelState *state = &detection->channels[index]; // 当前本路上下文。
        bool pending = false; // Device查询到的真实输入状态。
        DevFlameResult result; // 查询或读取结果。
        app_flame_update_age(state, now_ms);
        /*检查是否为可用通道。*/
        if (!state->initialized) {
            changed = !state->minute_incomplete || changed;
            state->minute_incomplete = true;
            continue;
        }
        /*判断是否为盲锁时间如果是盲锁时间则跳过 */
        if (state->blind) {
            if ((uint32_t)(now_ms - state->blind_start_ms) < state->blind_ms) {
                continue;
            }
            state->blind = false;
            changed = true;
        }
        /*如果app_flame_capture检测失败则重新进行检测*/
        if (state->awaiting_sample) {
            changed = app_flame_capture(state, now_ms) || changed;
            continue;
        }
        /*检测是否有新事件 Pending为是否有新事件 */
        result = state->device->pending(state->device, &pending);
        changed = app_flame_set_result(state, result) || changed;
        if ((result == DEV_FLAME_OK) && pending) {
            if (state->minute_event_count < APP_FLAME_MINUTE_COUNT_MAX) {
                ++state->minute_event_count; // 仅新事件入口计数，读取暂忙期间由awaiting_sample防止重复。
            }
            state->triggered = true; /* 必须在读数可能清除DOCI事件之前锁存。 */
            state->awaiting_sample = true;
            state->capture_start_ms = now_ms;
            changed = true;
            changed = app_flame_capture(state, now_ms) || changed;
        }
    }
    return changed;
}

/** @brief 保存清除结果，探头事件清除成功才清零本路分钟次数与历史，分钟计时保持连续。
 * @param detection 三路检测状态。
 * @param now_ms 执行清除时的开机毫秒数。
 * @param report 非空清除结果快照，地址及两个序号由调用方填写。 */
void AppFlameDetection_Clear(AppFlameDetection *detection, uint32_t now_ms, AppFlameClearReport *report)
{
    uint8_t index; // 当前清除的通道索引。
    if (report == NULL) {
        return;
    }
    report->uptime_ms = now_ms;
    report->not_initialized_mask = APP_FLAME_CLEAR_NO_CHANNELS;
    report->failed_mask = APP_FLAME_CLEAR_NO_CHANNELS;
    report->success = false;
    if (detection == NULL) {
        report->failed_mask = APP_FLAME_CLEAR_CHANNEL_MASK;
        for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
            report->results[index] = DEV_FLAME_ARGUMENT_ERROR;
        }
        return;
    }
    (void)app_flame_advance_minute(detection, now_ms);
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        AppFlameChannelState *state = &detection->channels[index]; // 本路锁存及探头状态。
        DevFlameResult result; // 本路物理事件清除结果。
        uint8_t channel_bit = (uint8_t)(APP_FLAME_CLEAR_CHANNEL_BIT << index); // 本路结果位图位置。
        if (!state->initialized) {
            report->not_initialized_mask |= channel_bit;
            report->results[index] = DEV_FLAME_NOT_READY;
            continue; /* 故障或缺配置通道无法确认物理事件已清除，保留原锁存。 */
        }
        result = ((state->device == NULL) || (state->device->clear == NULL)) ?
            DEV_FLAME_ARGUMENT_ERROR : state->device->clear(state->device);
        report->results[index] = result;
        (void)app_flame_set_result(state, result);
        if (result != DEV_FLAME_OK) {
            report->failed_mask |= channel_bit;
            continue; /* 不得将物理清除失败伪装成软件已清除。 */
        }
        state->triggered = false;
        state->minute_event_count = 0;
        state->minute_incomplete = true; // 人工清零使本分钟次数只覆盖清零后的部分。
        state->awaiting_sample = false;
        state->has_sample = false;
        memset(&state->sample, 0, sizeof(state->sample));
        state->sample_ms = 0U;
        state->sample_age_ms = 0U;
        state->age_tick_ms = now_ms;
        state->capture_start_ms = 0U;
        state->blind = true; /* DOCI复位后重新等待既有配置的保守盲锁期。 */
        state->blind_start_ms = now_ms;
    }
    report->success = ((report->not_initialized_mask | report->failed_mask) == APP_FLAME_CLEAR_NO_CHANNELS);
}

/** @brief 根据锁存和当前可用性产生固定三态快照。
 * @param detection 三路上下文。
 * @param now_ms 当前时间。
 * @param report 接收报告的对象，头部由上报模块维护。 */
void AppFlameDetection_Snapshot(AppFlameDetection *detection, uint32_t now_ms, AppFlameReport *report)
{
    uint8_t index; // 当前报告通道。
    bool any_triggered = false; // 是否至少有一路历史事件。
    bool all_ready = true; // 是否三路均能判断新的事件。
    if ((detection == NULL) || (report == NULL)) {
        return;
    }
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        AppFlameChannelState *state = &detection->channels[index]; // 本路内部状态。
        AppFlameChannelReport *channel = &report->channels[index]; // 本路线路快照。
        bool ready = state->initialized && !state->blind && !state->awaiting_sample && !state->fault; // 当前检测可用性。
        app_flame_update_age(state, now_ms);
        channel->channel_id = (uint8_t)(index + APP_FLAME_CHANNEL_ID_BASE);
        channel->decision = state->triggered ? APP_FLAME_TRIGGERED :
                            (ready ? APP_FLAME_NOT_DETECTED : APP_FLAME_UNKNOWN);
        channel->flags = APP_FLAME_EMPTY_FLAGS;
        if (ready) { channel->flags |= APP_FLAME_FLAG_DETECTION_READY; }
        if (state->has_sample) {
            channel->flags |= APP_FLAME_FLAG_HAS_SAMPLE;
            channel->flags |= state->sample.over_range ? APP_FLAME_FLAG_OVER_RANGE : APP_FLAME_FLAG_SAMPLE_VALID;
        }
        if (state->fault) { channel->flags |= APP_FLAME_FLAG_FAULT; }
        if (state->blind) { channel->flags |= APP_FLAME_FLAG_BLIND; }
        channel->result = state->result;
        channel->adc_raw = state->has_sample ? state->sample.adc_raw : APP_FLAME_EMPTY_RAW;
        channel->config_raw = state->has_sample ? state->sample.config_raw : APP_FLAME_EMPTY_RAW;
        channel->sample_age_ms = state->has_sample ? state->sample_age_ms : APP_FLAME_NO_SAMPLE_AGE;
        any_triggered = any_triggered || state->triggered;
        all_ready = all_ready && ready;
    }
    report->decision = any_triggered ? APP_FLAME_TRIGGERED :
                       (all_ready ? APP_FLAME_NOT_DETECTED : APP_FLAME_UNKNOWN);
    report->uptime_ms = now_ms;
}

/** @brief 推进分钟窗口并复用诊断快照生成三路次数，旧锁存结果不写入分钟报告。
 * @param detection 非空三路上下文。
 * @param now_ms 当前开机毫秒时间。
 * @param report 非空输出对象，保留调用方填写的类型、地址与序号。
 * @return 换分钟或首次标记未初始化、故障造成统计不完整时返回true。 */
bool AppFlameDetection_MinuteSnapshot(AppFlameDetection *detection, uint32_t now_ms,
                                    AppFlameMinuteReport *report)
{
    AppFlameReport diagnostics = {0}; // 复用既有状态位及原始数据生成规则，不上报其锁存结果。
    bool changed; // 分钟边界或本分钟不完整状态是否变化。
    uint8_t index; // 当前输出通道索引。
    if ((detection == NULL) || (report == NULL)) {
        return false;
    }
    changed = app_flame_advance_minute(detection, now_ms);
    AppFlameDetection_Snapshot(detection, now_ms, &diagnostics);
    report->uptime_ms = now_ms;
    report->minute_index = detection->minute_index;
    report->elapsed_ms = detection->minute_elapsed_ms;
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        AppFlameChannelState *state = &detection->channels[index]; // 本路计数及窗口完整性。
        const AppFlameChannelReport *source = &diagnostics.channels[index]; // 本路诊断字段来源。
        AppFlameMinuteChannelReport *channel = &report->channels[index]; // 本路分钟报告的输出位置。
        if (!state->initialized || state->fault) {
            changed = !state->minute_incomplete || changed;
            state->minute_incomplete = true;
        }
        channel->channel_id = source->channel_id;
        channel->flags = source->flags;
        if (state->minute_incomplete) {
            channel->flags |= APP_FLAME_FLAG_MINUTE_INCOMPLETE;
        }
        channel->result = source->result;
        channel->adc_raw = source->adc_raw;
        channel->config_raw = source->config_raw;
        channel->sample_age_ms = source->sample_age_ms;
        channel->event_count = state->minute_event_count;
    }
    return changed;
}

/** @brief 生成本轮初始化状态快照，成功状态不受运行期查询或采样错误码覆盖。
 * @param detection 三路检测上下文。
 * @param now_ms 当前开机毫秒时间。
 * @param report 接收初始化状态、配置结果及重试次数的对象，地址和序号由调用方维护。 */
void AppFlameDetection_InitSnapshot(const AppFlameDetection *detection, uint32_t now_ms, AppFlameInitReport *report)
{
    uint8_t index; // 初始化快照的当前通道索引。
    if ((detection == NULL) || (report == NULL)) {
        return;
    }
    report->uptime_ms = now_ms;
    report->retry_limit = APP_FLAME_CONFIG_MAX_RETRIES;
    report->retry_interval_ms = APP_FLAME_CONFIG_RETRY_INTERVAL_MS;
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        const AppFlameChannelState *state = &detection->channels[index]; // 本路最近一轮配置的内部状态。
        AppFlameInitChannelReport *channel = &report->channels[index]; // 本路初始化报告的输出位置。
        channel->channel_id = (uint8_t)(index + APP_FLAME_CHANNEL_ID_BASE);
        channel->result = state->config_result;
        channel->retries_done = state->retries_done;
        channel->status = !state->config_requested ? APP_FLAME_INIT_NOT_CONFIGURED :
            ((state->config_result == DEV_FLAME_OK) ? APP_FLAME_INIT_SUCCESS : APP_FLAME_INIT_FAILED);
    }
}
