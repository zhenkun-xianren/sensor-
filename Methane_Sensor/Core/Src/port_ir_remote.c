/* 基于驱动库 红外遥控/NEC 的解码状态机，适配本项目并补充校验。 */
#include "port_ir_remote.h"
#include "bsp_ir.h"
#include <stddef.h>
enum {
    IR_WAIT_MARK = 0U, IR_LEADER_SPACE, IR_BIT_MARK, IR_BIT_SPACE,
    IR_TRAILER_MARK, IR_REPEAT_MARK
};
static NecDecoder s_decoder; // 中断接收通道的 NEC 解码状态。
static volatile IrKeyEvent s_events[PORT_IR_QUEUE_SIZE]; // 单中断生产者、单任务消费者的固定事件缓冲。
static volatile uint8_t s_head; // 下一事件的写入索引。
static volatile uint8_t s_tail; // 下一事件的读取索引。
static uint8_t s_ready; // 板级初始化完成标志。
static DevIrRemote s_ir; // 对 APP 暴露的静态设备对象。
volatile uint32_t g_port_ir_dropped; // 缓冲满时丢弃的新事件数量。
/**
 * @brief 检查脉冲长度是否在包含边界的有效区间内。
 * @param value 实测微秒长度。
 * @param minimum 最小微秒长度。
 * @param maximum 最大微秒长度。
 * @return 有效返回1，否则返回0。
 */
static uint8_t between(uint32_t value, uint32_t minimum, uint32_t maximum)
{
    return value >= minimum && value <= maximum ? 1U : 0U;
}
/**
 * @brief 清除 NEC 解码器状态及前帧记录。
 * @param decoder 待清除的解码器。
 */
void PortIr_DecoderInit(NecDecoder *decoder)
{
    if (decoder == NULL) return;
    decoder->raw = 0U;
    decoder->last_raw = 0U;
    decoder->last_time_us = 0U;
    decoder->state = IR_WAIT_MARK;
    decoder->bit_count = 0U;
    decoder->has_last = 0U;
}
/**
 * @brief 校验命令反码并解析普通或扩展 NEC 地址。
 * @param raw 最低位先行组成的原始帧。
 * @param repeat 是否为重复帧。
 * @param event 接收已校验的按键事件。
 * @return 校验成功返回1，否则返回0。
 */
static uint8_t decode_code(uint32_t raw, uint8_t repeat, IrKeyEvent *event)
{
    uint8_t low = (uint8_t)raw; // 地址低字节。
    uint8_t high = (uint8_t)(raw >> PORT_IR_NEC_BYTE_BITS); // 地址高字节或低字节反码。
    uint8_t command = (uint8_t)(raw >> PORT_IR_NEC_COMMAND_SHIFT); // 命令字节。
    uint8_t inverse = (uint8_t)(raw >> PORT_IR_NEC_INVERSE_SHIFT); // 命令反码字节。
    if ((uint8_t)(command ^ inverse) != PORT_IR_NEC_BYTE_MASK) return 0U;
    event->address = (uint8_t)(low ^ high) == PORT_IR_NEC_BYTE_MASK ? low :
        (uint16_t)(low | ((uint16_t)high << PORT_IR_NEC_BYTE_BITS));
    event->command = command;
    event->repeat = repeat;
    event->raw_code = raw;
    return 1U;
}
/**
 * @brief 校验电平和时长，结束脉冲完成后才提交 NEC 帧。
 * @param decoder 接收状态对象。
 * @param level 已结束脉冲的电平。
 * @param duration_us 已结束脉冲的微秒长度。
 * @param now_us 脉冲结束的32位微秒时间戳。
 * @param event 接收有效帧信息的对象。
 * @return 完成有效帧返回1，其他情况返回0。
 */
uint8_t PortIr_FeedPulse(NecDecoder *decoder, uint8_t level,
                        uint32_t duration_us, uint32_t now_us, IrKeyEvent *event)
{
    if (decoder == NULL || event == NULL) return 0U;
    if (decoder->has_last != 0U &&
        (uint32_t)(now_us - decoder->last_time_us) > PORT_IR_NEC_REPEAT_VALID_US) {
        decoder->has_last = 0U;
    }
    if (duration_us > PORT_IR_NEC_FRAME_TIMEOUT_US) decoder->state = IR_WAIT_MARK;
    /* 合法低电平引导脉冲可从任意错误或不完整帧中重新同步。 */
    if (level == 0U && between(duration_us, PORT_IR_NEC_LEADER_MARK_MIN_US,
                               PORT_IR_NEC_LEADER_MARK_MAX_US)) {
        decoder->state = IR_LEADER_SPACE;
        decoder->raw = 0U;
        decoder->bit_count = 0U;
        return 0U;
    }
    switch (decoder->state) {
    case IR_LEADER_SPACE:
        if (level != 0U && between(duration_us, PORT_IR_NEC_LEADER_SPACE_MIN_US,
                                   PORT_IR_NEC_LEADER_SPACE_MAX_US)) {
            decoder->state = IR_BIT_MARK;
        } else if (level != 0U && between(duration_us, PORT_IR_NEC_REPEAT_SPACE_MIN_US,
                                          PORT_IR_NEC_REPEAT_SPACE_MAX_US)) {
            decoder->state = IR_REPEAT_MARK;
        } else decoder->state = IR_WAIT_MARK;
        break;
    case IR_BIT_MARK:
        decoder->state = level == 0U && between(duration_us, PORT_IR_NEC_BIT_MARK_MIN_US,
                                               PORT_IR_NEC_BIT_MARK_MAX_US) ? IR_BIT_SPACE : IR_WAIT_MARK;
        break;
    case IR_BIT_SPACE:
        if (level != 0U && (between(duration_us, PORT_IR_NEC_ZERO_SPACE_MIN_US,
                                    PORT_IR_NEC_ZERO_SPACE_MAX_US) ||
                            between(duration_us, PORT_IR_NEC_ONE_SPACE_MIN_US,
                                    PORT_IR_NEC_ONE_SPACE_MAX_US))) {
            if (duration_us >= PORT_IR_NEC_ONE_SPACE_MIN_US) {
                decoder->raw |= (uint32_t)1U << decoder->bit_count;
            }
            ++decoder->bit_count;
            decoder->state = decoder->bit_count == PORT_IR_NEC_FRAME_BITS ?
                IR_TRAILER_MARK : IR_BIT_MARK;
        } else decoder->state = IR_WAIT_MARK;
        break;
    case IR_TRAILER_MARK:
        decoder->state = IR_WAIT_MARK;
        if (level == 0U && between(duration_us, PORT_IR_NEC_BIT_MARK_MIN_US,
                                   PORT_IR_NEC_BIT_MARK_MAX_US) &&
            decode_code(decoder->raw, 0U, event) != 0U) {
            decoder->last_raw = decoder->raw;
            decoder->last_time_us = now_us;
            decoder->has_last = 1U;
            return 1U;
        }
        break;
    case IR_REPEAT_MARK:
        decoder->state = IR_WAIT_MARK;
        if (level == 0U && between(duration_us, PORT_IR_NEC_BIT_MARK_MIN_US,
                                   PORT_IR_NEC_BIT_MARK_MAX_US) && decoder->has_last != 0U) {
            decoder->last_time_us = now_us;
            return decode_code(decoder->last_raw, 1U, event);
        }
        break;
    default: decoder->state = IR_WAIT_MARK; break;
    }
    return 0U;
}
/**
 * @brief 在中断中解码并提交事件，满缓冲时保留已入队的事件。
 * @param level 已结束脉冲的电平。
 * @param duration_us 已结束脉冲的微秒长度。
 * @param now_us 脉冲结束的微秒时间戳。
 */
static void ir_on_pulse(uint8_t level, uint32_t duration_us, uint32_t now_us)
{
    IrKeyEvent event; // 本次脉冲产生的有效事件。
    uint8_t next; // 写入成功后新的队头索引。
    if (PortIr_FeedPulse(&s_decoder, level, duration_us, now_us, &event) == 0U) return;
    next = (uint8_t)((s_head + 1U) % PORT_IR_QUEUE_SIZE);
    if (next == s_tail) { ++g_port_ir_dropped; return; }
    s_events[s_head].address = event.address;
    s_events[s_head].command = event.command;
    s_events[s_head].repeat = event.repeat;
    s_events[s_head].raw_code = event.raw_code;
    /* 数据和索引均为 volatile；消费者在 BSP 临界区中读取完整事件。 */
    s_head = next;
}
/**
 * @brief 初始化协议状态并绑定板级脉冲采集回调。
 * @param self 请求初始化的设备对象。
 * @return 初始化成功返回1，失败返回0。
 */
static uint8_t ir_init(DevIrRemote *self)
{
    if (self != &s_ir || s_ready != 0U) return 0U;
    s_head = 0U;
    s_tail = 0U;
    g_port_ir_dropped = 0U;
    PortIr_DecoderInit(&s_decoder);
    s_ready = BSP_Ir_Init(ir_on_pulse);
    return s_ready;
}
/**
 * @brief 在短临界区中取出一个完整事件。
 * @param self 请求读取的设备对象。
 * @param event 接收完整事件的非空输出对象。
 * @return 成功返回DEV_IR_EVENT，空缓冲返回DEV_IR_EMPTY，失败返回DEV_IR_ERROR。
 */
static DevIrResult ir_read(DevIrRemote *self, IrKeyEvent *event)
{
    uint32_t mask; // 保存进入临界区前的中断状态。
    if (self != &s_ir || event == NULL || s_ready == 0U) return DEV_IR_ERROR;
    mask = BSP_Ir_Lock();
    if (s_tail == s_head) { BSP_Ir_Unlock(mask); return DEV_IR_EMPTY; }
    event->address = s_events[s_tail].address;
    event->command = s_events[s_tail].command;
    event->repeat = s_events[s_tail].repeat;
    event->raw_code = s_events[s_tail].raw_code;
    s_tail = (uint8_t)((s_tail + 1U) % PORT_IR_QUEUE_SIZE);
    BSP_Ir_Unlock(mask);
    return DEV_IR_EVENT;
}
/**
 * @brief 获取已经绑定全部方法的红外设备对象。
 * @return 静态红外设备对象指针。
 */
DevIrRemote *GetIrRemote(void)
{
    s_ir.init = ir_init;
    s_ir.read = ir_read;
    return &s_ir;
}
