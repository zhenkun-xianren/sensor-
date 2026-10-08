#include "port_key.h"
#include "bsp_key.h"
#include <stddef.h>
#include <string.h>

typedef struct {
    bool stable_pressed; // 消抖确认的按下状态。
    bool candidate_pressed; // 当前连续采样的候选输入。
    bool long_sent; // 本次按住是否已报告长按。
    bool press_history_valid; // 当前按住是否具有连续完整的采样历史。
    uint32_t candidate_since_ms; // 候选输入开始保持的时间。
    uint32_t pressed_since_ms; // 消抖按下确认时间，作为长按起点。
} KeyState;

static DevKey s_key; // 保存唯一设备对象与方法绑定。
static KeyState s_states[DEV_KEY_COUNT]; // 各键独立的消抖及长按状态。
static DevKeyEvent s_events[PORT_KEY_EVENT_CAPACITY]; // 不使用堆的事件环形缓冲。
static uint32_t s_read_index; // 下一条待读取事件的位置。
static uint32_t s_write_index; // 下一条新事件的写入位置。
static uint32_t s_event_count; // 已保存的事件数量。
static uint32_t s_last_scan_ms; // 上次成功采样的时间。
static bool s_have_sample = false; // 是否已经取得可连续比较的采样。
static bool s_ready = false; // 设备是否完成初始化。
static bool s_overflow = false; // 尚未由读取接口报告的事件丢失提示。

/**
 * @brief 在扫描临界区内追加事件；缓冲满保留旧事件并记录丢失。
 * @param id 产生事件的物理按键。
 * @param type 产生的事件类型。
 * @param now_ms 当前事件确认时间，单位毫秒。
 * @return 写入成功返回OK，容量不足返回OVERFLOW。
 */
static DevKeyResult key_push_event(DevKeyId id, DevKeyEventType type, uint32_t now_ms)
{
    if (s_event_count >= PORT_KEY_EVENT_CAPACITY) {
        s_overflow = true;
        return DEV_KEY_OVERFLOW;
    }
    s_events[s_write_index].id = id;
    s_events[s_write_index].type = type;
    s_events[s_write_index].timestamp_ms = now_ms;
    s_write_index = (s_write_index + 1U) % PORT_KEY_EVENT_CAPACITY;
    ++s_event_count;
    return DEV_KEY_OK;
}

/**
 * @brief 初始化板级输入并清除队列、采样历史与独立按键状态。
 * @param self 唯一按键设备对象。
 * @return 成功返回OK，对象无效返回INVALID_ARGUMENT，板级失败返回BOARD_ERROR。
 */
static DevKeyResult key_init(DevKey *self)
{
    uint32_t saved_mask; // 本次初始化前的中断状态。
    DevKeyResult result = DEV_KEY_BOARD_ERROR; // 保存初始化结果。
    if (self != &s_key) return DEV_KEY_INVALID_ARGUMENT;
    saved_mask = BSP_Key_EnterCritical();
    s_ready = false;
    s_have_sample = false;
    s_overflow = false;
    s_read_index = 0U;
    s_write_index = 0U;
    s_event_count = 0U;
    s_last_scan_ms = 0U;
    memset(s_states, 0, sizeof(s_states));
    if (BSP_Key_Init() == BSP_KEY_OK) {
        s_ready = true;
        result = DEV_KEY_OK;
    }
    BSP_Key_ExitCritical(saved_mask);
    return result;
}

/**
 * @brief 非阻塞更新全部按键；异常采样间隔取消未经证实的长短按历史。
 * @param self 唯一按键设备对象。
 * @param now_ms 单调毫秒时间，允许uint32_t自然回绕。
 * @return 成功返回OK，参数、初始化、板级读取或缓冲异常返回对应结果。
 */
static DevKeyResult key_scan(DevKey *self, uint32_t now_ms)
{
    uint32_t saved_mask; // 保护状态及事件队列的中断状态。
    uint32_t pressed_mask; // BSP返回的逻辑按下位图。
    uint32_t index; // 遍历三个独立按键。
    bool continuous; // 本次采样是否与上次采样足够连续。
    DevKeyResult result = DEV_KEY_OK; // 任一新事件丢失时保留溢出结果。
    if (self != &s_key) return DEV_KEY_INVALID_ARGUMENT;
    saved_mask = BSP_Key_EnterCritical();
    if (!s_ready) {
        BSP_Key_ExitCritical(saved_mask);
        return DEV_KEY_NOT_INITIALIZED;
    }
    if (BSP_Key_Read(&pressed_mask) != BSP_KEY_OK) {
        s_have_sample = false;
        BSP_Key_ExitCritical(saved_mask);
        return DEV_KEY_BOARD_ERROR;
    }
    continuous = s_have_sample &&
        (uint32_t)(now_ms - s_last_scan_ms) <= PORT_KEY_MAX_SCAN_GAP_MS;
    for (index = 0U; index < DEV_KEY_COUNT; ++index) {
        KeyState *state = &s_states[index]; // 当前按键私有状态。
        bool raw_pressed = (pressed_mask & (1UL << index)) != 0U; // 当前输入逻辑值。
        DevKeyId id = (DevKeyId)index; // 事件中的物理标识。
        if (!continuous || raw_pressed != state->candidate_pressed) {
            state->candidate_pressed = raw_pressed;
            state->candidate_since_ms = now_ms;
            if (!continuous) state->press_history_valid = false;
        }
        if (state->candidate_pressed != state->stable_pressed &&
            (uint32_t)(now_ms - state->candidate_since_ms) >= PORT_KEY_DEBOUNCE_MS) {
            state->stable_pressed = state->candidate_pressed;
            if (state->stable_pressed) {
                state->pressed_since_ms = now_ms;
                state->long_sent = false;
                state->press_history_valid = true;
                if (key_push_event(id, DEV_KEY_EVENT_PRESS, now_ms) != DEV_KEY_OK)
                    result = DEV_KEY_OVERFLOW;
            } else {
                if (key_push_event(id, DEV_KEY_EVENT_RELEASE, now_ms) != DEV_KEY_OK)
                    result = DEV_KEY_OVERFLOW;
                if (state->press_history_valid && !state->long_sent &&
                    key_push_event(id, DEV_KEY_EVENT_SHORT, now_ms) != DEV_KEY_OK)
                    result = DEV_KEY_OVERFLOW;
                state->press_history_valid = false;
            }
        }
        /* 释放正在消抖时不报告长按，避免临界时刻的短按被改成长按。 */
        if (state->stable_pressed && raw_pressed && state->press_history_valid &&
            !state->long_sent &&
            (uint32_t)(now_ms - state->pressed_since_ms) >= PORT_KEY_LONG_PRESS_MS) {
            state->long_sent = true;
            if (key_push_event(id, DEV_KEY_EVENT_LONG, now_ms) != DEV_KEY_OK)
                result = DEV_KEY_OVERFLOW;
        }
    }
    s_last_scan_ms = now_ms;
    s_have_sample = true;
    BSP_Key_ExitCritical(saved_mask);
    return result;
}

/**
 * @brief 优先报告一次队列溢出，之后按先进先出顺序取得已保存事件。
 * @param self 唯一按键设备对象。
 * @param event 事件输出指针，非成功返回时保持原值。
 * @return 成功返回OK，无事件返回NO_EVENT，其余返回对应具名异常。
 */
static DevKeyResult key_read(DevKey *self, DevKeyEvent *event)
{
    uint32_t saved_mask; // 保护队列读取与计数更新的中断状态。
    DevKeyResult result; // 保存本次读取状态。
    if (self != &s_key || event == NULL) return DEV_KEY_INVALID_ARGUMENT;
    saved_mask = BSP_Key_EnterCritical();
    if (!s_ready) result = DEV_KEY_NOT_INITIALIZED;
    else if (s_overflow) {
        s_overflow = false;
        result = DEV_KEY_OVERFLOW;
    } else if (s_event_count == 0U) result = DEV_KEY_NO_EVENT;
    else {
        *event = s_events[s_read_index];
        s_read_index = (s_read_index + 1U) % PORT_KEY_EVENT_CAPACITY;
        --s_event_count;
        result = DEV_KEY_OK;
    }
    BSP_Key_ExitCritical(saved_mask);
    return result;
}

/**
 * @brief 获取指定按键最近一次确认的消抖状态。
 * @param self 唯一按键设备对象。
 * @param id 范围为SW6、SW4和SW5。
 * @param pressed 非空输出指针，失败时保持原值。
 * @return 成功返回OK，参数无效或未初始化返回对应结果。
 */
static DevKeyResult key_get_state(DevKey *self, DevKeyId id, bool *pressed)
{
    uint32_t saved_mask; // 保护消抖状态读取的中断状态。
    DevKeyResult result; // 保存查询是否成功。
    if (self != &s_key || pressed == NULL || (uint32_t)id >= DEV_KEY_COUNT)
        return DEV_KEY_INVALID_ARGUMENT;
    saved_mask = BSP_Key_EnterCritical();
    if (!s_ready) result = DEV_KEY_NOT_INITIALIZED;
    else {
        *pressed = s_states[id].stable_pressed;
        result = DEV_KEY_OK;
    }
    BSP_Key_ExitCritical(saved_mask);
    return result;
}

/** @brief 获取设备对象并绑定全部方法。 @return 唯一静态设备对象。 */
DevKey *GetKey(void)
{
    s_key.init = key_init;
    s_key.scan = key_scan;
    s_key.read = key_read;
    s_key.get_state = key_get_state;
    return &s_key;
}
