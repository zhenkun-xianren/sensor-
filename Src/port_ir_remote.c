#include "port_ir_remote.h"
#include "bsp_ir.h"
#include <stddef.h>

#define PORT_IR_EVENT_QUEUE_SIZE 8U
#define PORT_IR_EVENT_QUEUE_MASK (PORT_IR_EVENT_QUEUE_SIZE - 1U)
#define PORT_IR_STATE_WAIT_MARK 0U
#define PORT_IR_STATE_LEADER_SPACE 1U
#define PORT_IR_STATE_BIT_MARK 2U
#define PORT_IR_STATE_BIT_SPACE 3U
#define PORT_IR_STATE_REPEAT_MARK 4U

static NecDecoder s_decoder; // 在红外边沿中断中持续更新的 NEC 解码器状态。
static volatile IrKeyEvent s_events[PORT_IR_EVENT_QUEUE_SIZE]; // 在中断和任务之间传递的已解码事件。
static volatile uint8_t s_head; // 红外事件环形队列的写入头索引。
static volatile uint8_t s_tail; // 红外事件环形队列的读取尾索引。
static uint8_t s_ir_initialized; // 红外板级接收和协议状态是否已成功初始化。
static DevIrRemote s_ir_remote; // 绑定红外 Device 接口和 Port 实现的静态对象。

/**
 * @brief 检查测得的脉冲时长是否处于闭区间范围内。
 * @param value 测得的脉冲时长，单位为微秒。
 * @param minimum 包含在范围内的最小值，单位为微秒。
 * @param maximum 包含在范围内的最大值，单位为微秒。
 * @return 数值在范围内时返回 1，否则返回 0。
 */
static uint8_t between(uint16_t value, uint16_t minimum, uint16_t maximum)
{
    return value >= minimum && value <= maximum ? 1U : 0U;
}

/**
 * @brief 将 NEC 遥控命令字节映射为显示数值的加减动作。
 * @param command 遥控器发送的 NEC 命令字节。
 * @return 返回加一、减一或无动作。
 */
static IrKeyAction command_action(uint8_t command)
{
    if (command == PORT_IR_COMMAND_INCREMENT) {
        return IR_KEY_ACTION_INCREMENT;
    }
    if (command == PORT_IR_COMMAND_DECREMENT) {
        return IR_KEY_ACTION_DECREMENT;
    }
    return IR_KEY_ACTION_NONE;
}

/**
 * @brief 校验 NEC 帧并将其命令字节转换为设备事件。
 * @param raw 完整的 32 位 NEC 帧。
 * @param repeat 当前事件为重复帧时传入非零值。
 * @param event 接收解码后的按键事件。
 * @return 命令反码校验通过返回 1，否则返回 0。
 */
static uint8_t decode_code(uint32_t raw, uint8_t repeat, IrKeyEvent *event)
{
    uint8_t address_low = (uint8_t)raw; // NEC 帧中的低地址字节。
    uint8_t address_high = (uint8_t)(raw >> PORT_IR_NEC_ADDRESS_HIGH_OFFSET_BITS); // NEC 帧中的高地址字节。
    uint8_t command = (uint8_t)(raw >> PORT_IR_NEC_COMMAND_OFFSET_BITS); // NEC 帧中的命令字节。
    uint8_t command_inverse = (uint8_t)(raw >> PORT_IR_NEC_COMMAND_INVERSE_OFFSET_BITS); // NEC 帧中的命令反码字节。

    if ((uint8_t)(command ^ command_inverse) != PORT_IR_NEC_BYTE_MASK) {
        return 0U;
    }
    event->address = (uint8_t)(address_low ^ address_high) == PORT_IR_NEC_BYTE_MASK ?
                     address_low : (uint16_t)(address_low |
                     ((uint16_t)address_high << PORT_IR_NEC_BYTE_WIDTH_BITS));
    event->command = command;
    event->digit = PortIr_CommandDigit(command);
    event->repeat = repeat;
    event->raw_code = raw;
    event->action = command_action(command);
    return 1U;
}

/**
 * @brief 在板级边沿测量完成后将脉冲送入 NEC 解码器。
 * @param level 脉冲期间检测到的逻辑电平。
 * @param duration_us 脉冲时长，单位为微秒。
 */
static void ir_pulse_callback(uint8_t level, uint16_t duration_us)
{
    PortIr_OnPulseIsr(level, duration_us);
}

/**
 * @brief 将 NEC 遥控命令字节映射为十进制数字。
 * @param command 遥控器发送的 NEC 命令字节。
 * @return 命令对应的数字 0 至 9；非数字命令返回 -1。
 */
int8_t PortIr_CommandDigit(uint8_t command)
{
    static const uint8_t codes[] = { // 保存遥控器数字键 0 至 9 对应的 NEC 命令码。
        PORT_IR_COMMAND_DIGIT_0, PORT_IR_COMMAND_DIGIT_1,
        PORT_IR_COMMAND_DIGIT_2, PORT_IR_COMMAND_DIGIT_3,
        PORT_IR_COMMAND_DIGIT_4, PORT_IR_COMMAND_DIGIT_5,
        PORT_IR_COMMAND_DIGIT_6, PORT_IR_COMMAND_DIGIT_7,
        PORT_IR_COMMAND_DIGIT_8, PORT_IR_COMMAND_DIGIT_9
    };
    uint8_t index; // 遍历数字键命令表的当前索引。

    for (index = 0U; index < sizeof(codes) / sizeof(codes[0]); ++index) {
        if (command == codes[index]) {
            return (int8_t)index;
        }
    }
    return -1;
}

/**
 * @brief 重置 NEC 解码状态和未完成的帧数据。
 * @param decoder 要初始化的解码器状态。
 */
void PortIr_DecoderInit(NecDecoder *decoder)
{
    if (decoder == NULL) {
        return;
    }
    decoder->raw = 0U;
    decoder->last_raw = 0U;
    decoder->state = PORT_IR_STATE_WAIT_MARK;
    decoder->bit_count = 0U;
    decoder->has_last = 0U;
}

/**
 * @brief 将一个已测时的红外脉冲送入 NEC 帧解码器。
 * @param decoder 要更新的解码器状态。
 * @param level 脉冲期间检测到的逻辑电平。
 * @param duration_us 脉冲时长，单位为微秒。
 * @param event 找到完整有效帧时接收解码后的事件。
 * @return 解码出按键事件返回 1，否则返回 0。
 */
uint8_t PortIr_FeedPulse(NecDecoder *decoder, uint8_t level,
                         uint16_t duration_us, IrKeyEvent *event)
{
    if (decoder == NULL || event == NULL) {
        return 0U;
    }
    if (duration_us > PORT_IR_NEC_FRAME_TIMEOUT_US) {
        decoder->state = PORT_IR_STATE_WAIT_MARK;
    }

    switch (decoder->state) {
    case PORT_IR_STATE_WAIT_MARK:
        if (level == 0U && between(duration_us, PORT_IR_NEC_LEADER_MARK_MIN_US,
                                   PORT_IR_NEC_LEADER_MARK_MAX_US) != 0U) {
            decoder->state = PORT_IR_STATE_LEADER_SPACE;
            decoder->raw = 0U;
            decoder->bit_count = 0U;
        }
        break;

    case PORT_IR_STATE_LEADER_SPACE:
        if (level != 0U && between(duration_us, PORT_IR_NEC_LEADER_SPACE_MIN_US,
                                   PORT_IR_NEC_LEADER_SPACE_MAX_US) != 0U) {
            decoder->state = PORT_IR_STATE_BIT_MARK;
        } else if (level != 0U &&
                   between(duration_us, PORT_IR_NEC_REPEAT_SPACE_MIN_US,
                           PORT_IR_NEC_REPEAT_SPACE_MAX_US) != 0U) {
            decoder->state = PORT_IR_STATE_REPEAT_MARK;
        } else {
            decoder->state = PORT_IR_STATE_WAIT_MARK;
        }
        break;

    case PORT_IR_STATE_REPEAT_MARK:
        decoder->state = PORT_IR_STATE_WAIT_MARK;
        if (level == 0U && between(duration_us, PORT_IR_NEC_BIT_MARK_MIN_US,
                                   PORT_IR_NEC_BIT_MARK_MAX_US) != 0U &&
            decoder->has_last != 0U) {
            return decode_code(decoder->last_raw, 1U, event);
        }
        break;

    case PORT_IR_STATE_BIT_MARK:
        if (level == 0U && between(duration_us, PORT_IR_NEC_BIT_MARK_MIN_US,
                                   PORT_IR_NEC_BIT_MARK_MAX_US) != 0U) {
            decoder->state = PORT_IR_STATE_BIT_SPACE;
        } else {
            decoder->state = PORT_IR_STATE_WAIT_MARK;
        }
        break;

    case PORT_IR_STATE_BIT_SPACE:
        if (level != 0U &&
            (between(duration_us, PORT_IR_NEC_ZERO_SPACE_MIN_US,
                      PORT_IR_NEC_ZERO_SPACE_MAX_US) != 0U ||
             between(duration_us, PORT_IR_NEC_ONE_SPACE_MIN_US,
                      PORT_IR_NEC_ONE_SPACE_MAX_US) != 0U)) {
            if (duration_us >= PORT_IR_NEC_ONE_SPACE_MIN_US) {
                decoder->raw |= (uint32_t)1U << decoder->bit_count;
            }
            ++decoder->bit_count;
            if (decoder->bit_count == PORT_IR_NEC_FRAME_BITS) {
                uint32_t raw = decoder->raw; // 保存本次完整接收的 NEC 原始帧。

                decoder->state = PORT_IR_STATE_WAIT_MARK;
                if (decode_code(raw, 0U, event) != 0U) {
                    decoder->last_raw = raw;
                    decoder->has_last = 1U;
                    return 1U;
                }
            } else {
                decoder->state = PORT_IR_STATE_BIT_MARK;
            }
        } else {
            decoder->state = PORT_IR_STATE_WAIT_MARK;
        }
        break;

    default:
        decoder->state = PORT_IR_STATE_WAIT_MARK;
        break;
    }
    return 0U;
}

/**
 * @brief 在中断中解码红外脉冲，并将完成的 NEC 按键事件加入队列。
 * @param level 脉冲期间检测到的逻辑电平。
 * @param duration_us 脉冲时长，单位为微秒。
 */
void PortIr_OnPulseIsr(uint8_t level, uint16_t duration_us)
{
    IrKeyEvent event; // 本次解码得到并准备写入队列的红外事件。
    uint8_t next; // 写入事件后待更新的队列头索引。

    if (PortIr_FeedPulse(&s_decoder, level, duration_us, &event) == 0U) {
        return;
    }
    next = (uint8_t)((s_head + 1U) & PORT_IR_EVENT_QUEUE_MASK);
    if (next == s_tail) {
        return;
    }
    s_events[s_head].address = event.address;
    s_events[s_head].command = event.command;
    s_events[s_head].digit = event.digit;
    s_events[s_head].repeat = event.repeat;
    s_events[s_head].raw_code = event.raw_code;
    s_events[s_head].action = event.action;
    s_head = next;
}

/**
 * @brief 初始化红外设备接口、NEC 解码器、事件队列和板级接收配置。
 * @param self 接收初始化请求的红外设备对象。
 * @return 板级输入和协议层初始化均成功返回 1，否则返回 0。
 */
static uint8_t ir_remote_init(DevIrRemote *self)
{
    (void)self;
    s_ir_initialized = 0U;
    s_head = 0U;
    s_tail = 0U;
    PortIr_DecoderInit(&s_decoder);
    if (BSP_Ir_Init(ir_pulse_callback) == 0U) {
        return 0U;
    }
    s_ir_initialized = 1U;
    return 1U;
}

/**
 * @brief 从红外事件队列中取出下一个已解码的按键事件。
 * @param self 接收读取请求的红外设备对象。
 * @param event 接收下一个按键事件的输出对象。
 * @return 取出事件返回 1；队列为空、设备未初始化或参数无效返回 0。
 */
static uint8_t ir_remote_read_event(DevIrRemote *self, IrKeyEvent *event)
{
    uint8_t tail; // 本次待读取事件的队列尾索引快照。

    (void)self;
    if (s_ir_initialized == 0U || event == NULL || s_tail == s_head) {
        return 0U;
    }
    tail = s_tail;
    event->address = s_events[tail].address;
    event->command = s_events[tail].command;
    event->digit = s_events[tail].digit;
    event->repeat = s_events[tail].repeat;
    event->raw_code = s_events[tail].raw_code;
    event->action = s_events[tail].action;
    s_tail = (uint8_t)((tail + 1U) & PORT_IR_EVENT_QUEUE_MASK);
    return 1U;
}

/**
 * @brief 获取红外遥控设备接口并绑定 Port 实现。
 * @return 指向静态红外遥控设备接口的指针。
 */
DevIrRemote *GetIrRemote(void)
{
    s_ir_remote.init = ir_remote_init;
    s_ir_remote.readEvent = ir_remote_read_event;
    return &s_ir_remote;
}
