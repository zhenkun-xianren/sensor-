#include "port_ir_remote.h"
#include "bsp_ir.h"
#include <stddef.h>

enum {
    IR_WAIT_MARK = 0U,
    IR_LEADER_SPACE,
    IR_BIT_MARK,
    IR_BIT_SPACE,
    IR_REPEAT_MARK,
    IR_STOP_MARK
};

static NecDecoder s_decoder;                       // 中断使用的单实例解码器。
static volatile IrKeyEvent s_events[PORT_IR_QUEUE_SIZE]; // 固定事件环形缓冲。
static volatile uint8_t s_head;                    // 下次写入位置。
static volatile uint8_t s_tail;                    // 下次读取位置。
static volatile uint8_t s_initialized;             // 设备初始化成功标志。
static volatile uint32_t s_overflow_count;          // 丢弃新事件的累计次数。
static DevIrRemote s_device;                       // APP可见静态设备对象。

/**
 * @brief 判断脉冲时长是否位于容差闭区间。
 * @param value 实际微秒时长。
 * @param minimum 最小允许微秒时长。
 * @param maximum 最大允许微秒时长。
 * @return 区间内为1，否则为0。
 */
static uint8_t between(uint32_t value, uint32_t minimum, uint32_t maximum)
{
    return (value >= minimum && value <= maximum) ? 1U : 0U;
}

/**
 * @brief 将库中命令映射为数字提示。
 * @param command NEC命令字节。
 * @return 数字0至9，其他命令为-1。
 */
int8_t PortIr_CommandDigit(uint8_t command)
{
    static const uint8_t codes[] = {               // 库中数字0至9的固定命令表。
        PORT_IR_COMMAND_DIGIT_0, PORT_IR_COMMAND_DIGIT_1,
        PORT_IR_COMMAND_DIGIT_2, PORT_IR_COMMAND_DIGIT_3,
        PORT_IR_COMMAND_DIGIT_4, PORT_IR_COMMAND_DIGIT_5,
        PORT_IR_COMMAND_DIGIT_6, PORT_IR_COMMAND_DIGIT_7,
        PORT_IR_COMMAND_DIGIT_8, PORT_IR_COMMAND_DIGIT_9
    };
    uint8_t index;                                // 当前命令表索引。
    for (index = 0U; index < sizeof(codes) / sizeof(codes[0]); ++index) {
        if (command == codes[index]) return (int8_t)index;
    }
    return -1;
}

/**
 * @brief 查询库中加减键提示，不执行应用动作。
 * @param command NEC命令字节。
 * @return 库中加一、减一或无映射。
 */
static IrKeyAction command_action(uint8_t command)
{
    if (command == PORT_IR_COMMAND_INCREMENT) return IR_KEY_ACTION_INCREMENT;
    if (command == PORT_IR_COMMAND_DECREMENT) return IR_KEY_ACTION_DECREMENT;
    return IR_KEY_ACTION_NONE;
}

/**
 * @brief 初始化解码器并清空重复帧历史。
 * @param decoder 解码器对象，空指针不操作。
 */
void PortIr_DecoderInit(NecDecoder *decoder)
{
    if (decoder == NULL) return;
    decoder->raw = 0U;
    decoder->last_raw = 0U;
    decoder->last_event_us = 0U;
    decoder->state = IR_WAIT_MARK;
    decoder->bit_count = 0U;
    decoder->has_last = 0U;
}

/**
 * @brief 校验命令反码并提取NEC标准或扩展地址。
 * @param raw 按LSB先行组装的完整帧。
 * @param repeat 完整帧为0，重复帧为1。
 * @param event 校验成功后的事件输出。
 * @return 校验通过为1，命令反码错误为0。
 */
static uint8_t decode_code(uint32_t raw, uint8_t repeat, IrKeyEvent *event)
{
    uint8_t low = (uint8_t)raw;                    // 地址低字节。
    uint8_t high = (uint8_t)(raw >> PORT_IR_NEC_ADDRESS_HIGH_OFFSET_BITS); // 地址高字节或反码。
    uint8_t command = (uint8_t)(raw >> PORT_IR_NEC_COMMAND_OFFSET_BITS); // 命令字节。
    uint8_t inverse = (uint8_t)(raw >> PORT_IR_NEC_COMMAND_INVERSE_OFFSET_BITS); // 命令反码。
    if ((uint8_t)(command ^ inverse) != PORT_IR_NEC_BYTE_MASK) return 0U;
    event->address = ((uint8_t)(low ^ high) == PORT_IR_NEC_BYTE_MASK) ?
        low : (uint16_t)(low | ((uint16_t)high << PORT_IR_NEC_BYTE_WIDTH_BITS));
    event->command = command;
    event->digit = PortIr_CommandDigit(command);
    event->repeat = repeat;
    event->raw_code = raw;
    event->action = command_action(command);
    return 1U;
}

/**
 * @brief 解析已结束脉冲，并在末尾低脉冲通过后输出事件。
 * @param decoder 解码器对象。
 * @param level 已结束脉冲电平0或1，其他值表示捕获丢失。
 * @param duration_us 脉冲微秒时长。
 * @param end_us 脉冲结束的32位微秒时间戳。
 * @param event 有效事件输出。
 * @return 产生事件为1，参数错误或未产生事件为0。
 */
uint8_t PortIr_FeedPulse(NecDecoder *decoder, uint8_t level,
                         uint32_t duration_us, uint32_t end_us, IrKeyEvent *event)
{
    if (decoder == NULL || event == NULL) return 0U;
    if (level > 1U && level != BSP_IR_LEVEL_IDLE) {
        PortIr_DecoderInit(decoder);
        return 0U;
    }
    /* 无符号差支持计数器自然回绕；长空闲必须在截断前参与判定。 */
    if (duration_us > PORT_IR_NEC_REPEAT_MAX_AGE_US ||
        (decoder->has_last != 0U &&
         (uint32_t)(end_us - decoder->last_event_us) > PORT_IR_NEC_REPEAT_MAX_AGE_US)) {
        decoder->has_last = 0U;
    }
    if (duration_us > PORT_IR_NEC_FRAME_TIMEOUT_US) {
        if (decoder->state != IR_WAIT_MARK) decoder->has_last = 0U;
        decoder->state = IR_WAIT_MARK;
        decoder->bit_count = 0U;
    }
    /* 极低频计数器溢出通知只检查过期；不破坏跨回绕接收中的短脉冲。 */
    if (level == BSP_IR_LEVEL_IDLE) return 0U;
    /* 在任意异常状态遇到新引导低脉冲时直接重新同步。 */
    if (level == 0U && between(duration_us, PORT_IR_NEC_LEADER_MARK_MIN_US,
                                PORT_IR_NEC_LEADER_MARK_MAX_US)) {
        if (decoder->state != IR_WAIT_MARK) decoder->has_last = 0U;
        decoder->state = IR_LEADER_SPACE;
        decoder->raw = 0U;
        decoder->bit_count = 0U;
        return 0U;
    }
    switch (decoder->state) {
    case IR_WAIT_MARK:
        return 0U;
    case IR_LEADER_SPACE:
        if (level == 1U && between(duration_us, PORT_IR_NEC_LEADER_SPACE_MIN_US,
                                    PORT_IR_NEC_LEADER_SPACE_MAX_US)) {
            decoder->state = IR_BIT_MARK;
            return 0U;
        }
        if (level == 1U && between(duration_us, PORT_IR_NEC_REPEAT_SPACE_MIN_US,
                                    PORT_IR_NEC_REPEAT_SPACE_MAX_US)) {
            decoder->state = IR_REPEAT_MARK;
            return 0U;
        }
        break;
    case IR_BIT_MARK:
        if (level == 0U && between(duration_us, PORT_IR_NEC_BIT_MARK_MIN_US,
                                    PORT_IR_NEC_BIT_MARK_MAX_US)) {
            decoder->state = IR_BIT_SPACE;
            return 0U;
        }
        break;
    case IR_BIT_SPACE:
        if (level == 1U &&
            (between(duration_us, PORT_IR_NEC_ZERO_SPACE_MIN_US, PORT_IR_NEC_ZERO_SPACE_MAX_US) ||
             between(duration_us, PORT_IR_NEC_ONE_SPACE_MIN_US, PORT_IR_NEC_ONE_SPACE_MAX_US))) {
            if (duration_us >= PORT_IR_NEC_ONE_SPACE_MIN_US) {
                decoder->raw |= (uint32_t)1U << decoder->bit_count;
            }
            ++decoder->bit_count;
            decoder->state = (decoder->bit_count == PORT_IR_NEC_FRAME_BITS) ? IR_STOP_MARK : IR_BIT_MARK;
            return 0U;
        }
        break;
    case IR_STOP_MARK:
        if (level == 0U && between(duration_us, PORT_IR_NEC_BIT_MARK_MIN_US,
                                    PORT_IR_NEC_BIT_MARK_MAX_US) &&
            decode_code(decoder->raw, 0U, event) != 0U) {
            decoder->last_raw = decoder->raw;
            decoder->last_event_us = end_us;
            decoder->has_last = 1U;
            decoder->state = IR_WAIT_MARK;
            return 1U;
        }
        break;
    case IR_REPEAT_MARK:
        if (level == 0U && between(duration_us, PORT_IR_NEC_BIT_MARK_MIN_US,
                                    PORT_IR_NEC_BIT_MARK_MAX_US) && decoder->has_last != 0U &&
            decode_code(decoder->last_raw, 1U, event) != 0U) {
            decoder->last_event_us = end_us;
            decoder->state = IR_WAIT_MARK;
            return 1U;
        }
        break;
    default:
        break;
    }
    /* 错误帧不沿用旧按键历史，避免噪声产生旧按键的重复事件。 */
    decoder->state = IR_WAIT_MARK;
    decoder->bit_count = 0U;
    decoder->has_last = 0U;
    return 0U;
}

/**
 * @brief 在中断中解码脉冲并入队；队列满时丢弃最新事件。
 * @param level 已结束脉冲电平或捕获丢失标记。
 * @param duration_us 脉冲微秒时长。
 * @param end_us 脉冲结束的微秒时间戳。
 */
static void ir_on_pulse(uint8_t level, uint32_t duration_us, uint32_t end_us)
{
    IrKeyEvent event;                             // 本次有效解码事件。
    uint8_t next;                                // 环形队列的下一个写位置。
    if (s_initialized == 0U) return;
    if (PortIr_FeedPulse(&s_decoder, level, duration_us, end_us, &event) == 0U) return;
    next = (uint8_t)((s_head + 1U) % PORT_IR_QUEUE_SIZE);
    if (next == s_tail) {
        if (s_overflow_count != UINT32_MAX) ++s_overflow_count;
        return;
    }
    s_events[s_head] = event;
    s_head = next;
}

/**
 * @brief 初始化设备及固定队列，硬件失败时保持未就绪状态。
 * @param self 本模块静态设备对象。
 * @return 成功为DEV_IR_OK，参数错误或硬件失败返回对应状态。
 */
static DevIrResult ir_init(DevIrRemote *self)
{
    uint32_t mask;                               // 调用前的中断屏蔽状态。
    int result;                                  // 板级初始化结果。
    if (self != &s_device) return DEV_IR_ERROR_ARGUMENT;
    mask = BSP_Ir_EnterCritical();
    s_initialized = 0U;
    s_head = 0U;
    s_tail = 0U;
    s_overflow_count = 0U;
    PortIr_DecoderInit(&s_decoder);
    result = BSP_Ir_Init(ir_on_pulse);
    if (result == 0) s_initialized = 1U;
    BSP_Ir_ExitCritical(mask);
    return (result == 0) ? DEV_IR_OK : DEV_IR_ERROR_INIT;
}

/**
 * @brief 在短临界区内读取完整事件，支持多个串行调用方。
 * @param self 本模块静态设备对象。
 * @param event 事件输出指针。
 * @return 成功、空队列、参数错误或未初始化状态。
 */
static DevIrResult ir_read_event(DevIrRemote *self, IrKeyEvent *event)
{
    uint32_t mask;                               // 调用前中断状态。
    if (self != &s_device || event == NULL) return DEV_IR_ERROR_ARGUMENT;
    mask = BSP_Ir_EnterCritical();
    if (s_initialized == 0U) {
        BSP_Ir_ExitCritical(mask);
        return DEV_IR_ERROR_NOT_READY;
    }
    if (s_tail == s_head) {
        BSP_Ir_ExitCritical(mask);
        return DEV_IR_NO_EVENT;
    }
    *event = s_events[s_tail];
    s_tail = (uint8_t)((s_tail + 1U) % PORT_IR_QUEUE_SIZE);
    BSP_Ir_ExitCritical(mask);
    return DEV_IR_OK;
}

/**
 * @brief 在临界区读取饱和溢出计数。
 * @param self 本模块静态设备对象。
 * @param count 累计次数输出指针。
 * @return 成功、参数错误或未初始化状态。
 */
static DevIrResult ir_get_overflow(DevIrRemote *self, uint32_t *count)
{
    uint32_t mask;                               // 调用前中断状态。
    if (self != &s_device || count == NULL) return DEV_IR_ERROR_ARGUMENT;
    mask = BSP_Ir_EnterCritical();
    if (s_initialized == 0U) {
        BSP_Ir_ExitCritical(mask);
        return DEV_IR_ERROR_NOT_READY;
    }
    *count = s_overflow_count;
    BSP_Ir_ExitCritical(mask);
    return DEV_IR_OK;
}

/**
 * @brief 绑定静态接口，不初始化硬件。
 * @return 本模块唯一设备对象。
 */
DevIrRemote *GetIrRemote(void)
{
    s_device.init = ir_init;
    s_device.read_event = ir_read_event;
    s_device.get_overflow_count = ir_get_overflow;
    return &s_device;
}
