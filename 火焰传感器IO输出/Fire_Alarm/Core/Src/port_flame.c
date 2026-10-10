#include <stddef.h>
#include "port_flame.h"
#include "bsp_flame.h"

/** @brief 单路接口及最近一次已核对的配置；没有动态内存依赖。 */
typedef struct {
    DevFlame device;          // 本路对外接口对象。
    DevFlameChannel channel;  // 本路逻辑通道，不存放 GPIO。
    bool initialized;         // 是否写入并回读核对成功。
    uint32_t config_word;     // 最近确认的 25 位配置字。
} FlameState;

/* 提前声明已有方法，供三路静态对象在程序启动时完成接口绑定。 */
static DevFlameResult flame_init(DevFlame *self, const DevFlameConfig *config);
static DevFlameResult flame_configure(DevFlame *self, const DevFlameConfig *config);
static DevFlameResult flame_read(DevFlame *self, DevFlameReadMode mode, DevFlameSample *sample);
static DevFlameResult flame_pending(DevFlame *self, bool *pending);
static DevFlameResult flame_clear(DevFlame *self);

/* 配置字按静态初始化规则清零；只有初始化及回读核对成功后才成为有效配置。 */
static FlameState s_flame_states[DEV_FLAME_CHANNEL_COUNT] = { // 三路接口预先绑定，初始均未就绪。
    [DEV_FLAME_U22] = {
        .device = {
            .init = flame_init,
            .configure = flame_configure,
            .read = flame_read,
            .pending = flame_pending,
            .clear = flame_clear
        },
        .channel = DEV_FLAME_U22,
        .initialized = false
    },
    [DEV_FLAME_U23] = {
        .device = {
            .init = flame_init,
            .configure = flame_configure,
            .read = flame_read,
            .pending = flame_pending,
            .clear = flame_clear
        },
        .channel = DEV_FLAME_U23,
        .initialized = false
    },
    [DEV_FLAME_U24] = {
        .device = {
            .init = flame_init,
            .configure = flame_configure,
            .read = flame_read,
            .pending = flame_pending,
            .clear = flame_clear
        },
        .channel = DEV_FLAME_U24,
        .initialized = false
    }
};

/** @brief 核对对象归属，拒绝调用方伪造或复制的接口对象。
 * @param self 调用者传入的对象。
 * @return 本驱动静态状态；非法对象返回 NULL。 */
static FlameState *flame_get_state(DevFlame *self)
{
    uint32_t index; // 遍历三个静态对象，不将通道索引作为状态码。
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        if (self == &s_flame_states[index].device) {
            return &s_flame_states[index];
        }
    }
    return NULL;
}

/** @brief 校验字段及检测模式组合；校验失败不访问硬件。
 * @param config 待校验配置。
 * @return 配置有效返回 OK，否则 ARGUMENT_ERROR。 */
static DevFlameResult flame_validate_config(const DevFlameConfig *config)
{
    if ((config == NULL) || (config->blind_time_code > PORT_FLAME_BLIND_MAX) ||
        (config->pulse_count_code > PORT_FLAME_COUNT_MAX) ||
        (config->window_time_code > PORT_FLAME_COUNT_MAX)) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    if (((config->interrupt_source != DEV_FLAME_INTERRUPT_EVENT) &&
         (config->interrupt_source != DEV_FLAME_INTERRUPT_ADC)) ||
        ((config->adc_source != DEV_FLAME_SOURCE_BPF) &&
         (config->adc_source != DEV_FLAME_SOURCE_LPF) &&
         (config->adc_source != DEV_FLAME_SOURCE_SUPPLY) &&
         (config->adc_source != DEV_FLAME_SOURCE_TEMPERATURE)) ||
        ((config->high_pass != DEV_FLAME_HPF_0P4_HZ) &&
         (config->high_pass != DEV_FLAME_HPF_0P2_HZ)) ||
        ((config->pulse_mode != DEV_FLAME_COUNT_ALTERNATING) &&
         (config->pulse_mode != DEV_FLAME_COUNT_DIRECT))) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    if (config->detection_enabled &&
        (config->adc_source != DEV_FLAME_SOURCE_BPF) &&
        (config->adc_source != DEV_FLAME_SOURCE_LPF)) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    return DEV_FLAME_OK;
}

/** @brief 按手册编码配置，固定 bit4=1、bit3=0、bit1=0。
 * @param config 已通过字段校验的配置。
 * @return SERIN 的 25 位配置字。 */
static uint32_t flame_pack_config(const DevFlameConfig *config)
{
    uint32_t word = PORT_FLAME_VREG_DISABLED; // 保持手册规定的 VREG 关闭。
    word |= (uint32_t)config->sensitivity << PORT_FLAME_SENSITIVITY_SHIFT;
    word |= (uint32_t)config->blind_time_code << PORT_FLAME_BLIND_SHIFT;
    word |= (uint32_t)config->pulse_count_code << PORT_FLAME_PULSE_SHIFT;
    word |= (uint32_t)config->window_time_code << PORT_FLAME_WINDOW_SHIFT;
    word |= (uint32_t)config->detection_enabled << PORT_FLAME_DETECT_SHIFT;
    word |= (uint32_t)config->interrupt_source << PORT_FLAME_INTERRUPT_SHIFT;
    word |= (uint32_t)config->adc_source << PORT_FLAME_SOURCE_SHIFT;
    word |= (uint32_t)config->high_pass << PORT_FLAME_HPF_SHIFT;
    word |= (uint32_t)config->pulse_mode;
    return word;
}

/** @brief 解包完整原始帧，不猜测有符号格式或执行未标定换算。
 * @param raw_frame 高位已清零的 40 位帧。
 * @param sample 接收解包结果的临时对象。
 */
static void flame_decode_sample(uint64_t raw_frame, DevFlameSample *sample)
{
    sample->frame_raw = raw_frame;
    sample->config_raw = (uint32_t)raw_frame & PORT_FLAME_CONFIG_MASK;
    sample->adc_raw = (uint16_t)((raw_frame >> PORT_FLAME_ADC_SHIFT) & PORT_FLAME_ADC_MASK);
    sample->over_range = ((raw_frame >> PORT_FLAME_RANGE_SHIFT) == PORT_FLAME_RANGE_FAULT);
}

/** @brief 完整写入配置并强制读回；只有核对一致才提交就绪状态。
 * @param state 本路静态状态。
 * @param config 已校验的完整配置。
 * @return 成功返回 OK，其他返回板级错误或 CONFIG_MISMATCH。 */
static DevFlameResult flame_apply_config(FlameState *state, const DevFlameConfig *config)
{
    uint32_t word = flame_pack_config(config); // 保存期望的 25 位配置。
    uint64_t raw_frame; // 接收实际读回的完整帧。
    DevFlameResult result; // 逐阶段传播原错误。
    state->initialized = false;
    result = BSP_Flame_WriteBits(state->channel, word, PORT_FLAME_CONFIG_BITS);
    if (result != DEV_FLAME_OK) {
        return result;
    }
    result = BSP_Flame_ReadBits(state->channel, true, PORT_FLAME_FRAME_BITS, &raw_frame);
    if (result != DEV_FLAME_OK) {
        return result;
    }
    if (((uint32_t)raw_frame & PORT_FLAME_CONFIG_MASK) != word) {
        return DEV_FLAME_CONFIG_MISMATCH;
    }
    state->config_word = word;
    state->initialized = true;
    return DEV_FLAME_OK;
}

/** @brief 初始化本路 GPIO/计时并配置探头，不修改其他通道状态。
 * @param self 本路静态接口对象。
 * @param config 调用者提供的有效配置。
 * @return OK 或参数、板级、时序、回读核对错误。 */
static DevFlameResult flame_init(DevFlame *self, const DevFlameConfig *config)
{
    FlameState *state = flame_get_state(self); // 保存经归属核对的本路状态。
    DevFlameResult result = flame_validate_config(config); // 先校验，避免非法配置操作引脚。
    if ((state == NULL) || (result != DEV_FLAME_OK)) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    result = BSP_Flame_Init(state->channel);
    if (result != DEV_FLAME_OK) {
        state->initialized = false;
        return result;
    }
    return flame_apply_config(state, config);
}

/** @brief 重新配置已就绪探头；校验失败不破坏原配置。
 * @param self 本路静态接口对象。
 * @param config 待应用的完整配置。
 * @return OK 或参数、未就绪、板级、时序、回读核对错误。 */
static DevFlameResult flame_configure(DevFlame *self, const DevFlameConfig *config)
{
    FlameState *state = flame_get_state(self); // 保存本路状态。
    DevFlameResult result = flame_validate_config(config); // 配置字编码之前检查范围。
    if ((state == NULL) || (result != DEV_FLAME_OK)) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    if (!state->initialized) {
        return DEV_FLAME_NOT_READY;
    }
    return flame_apply_config(state, config);
}

/** @brief 完整读取并核对配置；错误或无数据时不改写输出。
 * @param self 本路静态接口对象。
 * @param mode 就绪读取或强制读取。
 * @param sample 成功时接收有效原始帧。
 * @return OK、PENDING 或对应参数、状态、板级错误。 */
static DevFlameResult flame_read(DevFlame *self, DevFlameReadMode mode, DevFlameSample *sample)
{
    FlameState *state = flame_get_state(self); // 本路最近确认的配置状态。
    DevFlameSample temporary; // 仅在完整读取及核对成功后提交给调用方。
    uint64_t raw_frame; // 保存本次物理层位流。
    DevFlameResult result; // 保存 BSP 的读数结果。
    if ((state == NULL) || (sample == NULL) ||
        ((mode != DEV_FLAME_READ_READY) && (mode != DEV_FLAME_READ_FORCE))) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    if (!state->initialized) {
        return DEV_FLAME_NOT_READY;
    }
    result = BSP_Flame_ReadBits(state->channel, mode == DEV_FLAME_READ_FORCE,
                                PORT_FLAME_FRAME_BITS, &raw_frame);
    if (result != DEV_FLAME_OK) {
        return result;
    }
    flame_decode_sample(raw_frame, &temporary);
    if (temporary.config_raw != state->config_word) {
        state->initialized = false;
        return DEV_FLAME_CONFIG_MISMATCH;
    }
    *sample = temporary;
    return DEV_FLAME_OK;
}

/** @brief 只查询 DOCI，不消费配置中断源对应的事件。
 * @param self 本路静态接口对象。
 * @param pending 接收事件/数据是否就绪。
 * @return OK 或参数、未就绪、板级错误。 */
static DevFlameResult flame_pending(DevFlame *self, bool *pending)
{
    FlameState *state = flame_get_state(self); // 本路静态状态。
    if ((state == NULL) || (pending == NULL)) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    if (!state->initialized) {
        return DEV_FLAME_NOT_READY;
    }
    return BSP_Flame_Pending(state->channel, pending);
}

/** @brief 拉低 DOCI 清除本路事件，不改变检测配置。
 * @param self 本路静态接口对象。
 * @return OK 或参数、未就绪、板级错误。 */
static DevFlameResult flame_clear(DevFlame *self)
{
    FlameState *state = flame_get_state(self); // 保存本路状态。
    if (state == NULL) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    if (!state->initialized) {
        return DEV_FLAME_NOT_READY;
    }
    return BSP_Flame_Clear(state->channel);
}

/** @brief 获取本路静态设备接口，不改变设备状态。
 * @param channel U22、U23 或 U24。
 * @return 本路接口；非法通道返回 NULL。 */
DevFlame *GetFlame(DevFlameChannel channel)
{
    if ((channel != DEV_FLAME_U22) &&
        (channel != DEV_FLAME_U23) &&
        (channel != DEV_FLAME_U24)) {
        return NULL;
    }

    return &s_flame_states[channel].device;
}
