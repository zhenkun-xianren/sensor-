#ifndef PORT_IR_REMOTE_H
#define PORT_IR_REMOTE_H

#include <stdint.h>
#include "dev_ir_remote.h"

/* 以下容差及键码逐项沿用驱动库红外遥控/NEC。 */
#define PORT_IR_NEC_FRAME_BITS 32U
#define PORT_IR_NEC_FRAME_TIMEOUT_US 15000UL
#define PORT_IR_NEC_REPEAT_MAX_AGE_US 150000UL
#define PORT_IR_NEC_LEADER_MARK_MIN_US 7000UL
#define PORT_IR_NEC_LEADER_MARK_MAX_US 11000UL
#define PORT_IR_NEC_LEADER_SPACE_MIN_US 3000UL
#define PORT_IR_NEC_LEADER_SPACE_MAX_US 6000UL
#define PORT_IR_NEC_REPEAT_SPACE_MIN_US 1500UL
#define PORT_IR_NEC_REPEAT_SPACE_MAX_US 2800UL
#define PORT_IR_NEC_BIT_MARK_MIN_US 300UL
#define PORT_IR_NEC_BIT_MARK_MAX_US 1000UL
#define PORT_IR_NEC_ZERO_SPACE_MIN_US 300UL
#define PORT_IR_NEC_ZERO_SPACE_MAX_US 1000UL
#define PORT_IR_NEC_ONE_SPACE_MIN_US 1200UL
#define PORT_IR_NEC_ONE_SPACE_MAX_US 2500UL
#define PORT_IR_NEC_BYTE_WIDTH_BITS 8U
#define PORT_IR_NEC_ADDRESS_HIGH_OFFSET_BITS 8U
#define PORT_IR_NEC_COMMAND_OFFSET_BITS 16U
#define PORT_IR_NEC_COMMAND_INVERSE_OFFSET_BITS 24U
#define PORT_IR_NEC_BYTE_MASK 0xFFU
#define PORT_IR_QUEUE_SIZE 8U

enum {
    PORT_IR_COMMAND_DIGIT_0 = 0x16U,
    PORT_IR_COMMAND_DIGIT_1 = 0x0CU,
    PORT_IR_COMMAND_DIGIT_2 = 0x18U,
    PORT_IR_COMMAND_DIGIT_3 = 0x5EU,
    PORT_IR_COMMAND_DIGIT_4 = 0x08U,
    PORT_IR_COMMAND_DIGIT_5 = 0x1CU,
    PORT_IR_COMMAND_DIGIT_6 = 0x5AU,
    PORT_IR_COMMAND_DIGIT_7 = 0x42U,
    PORT_IR_COMMAND_DIGIT_8 = 0x52U,
    PORT_IR_COMMAND_DIGIT_9 = 0x4AU,
    PORT_IR_COMMAND_INCREMENT = 0x09U,
    PORT_IR_COMMAND_DECREMENT = 0x15U
};

/* Port内部状态，不属于APP可见设备契约。 */
typedef struct {
    uint32_t raw;             // 本次正在接收的帧。
    uint32_t last_raw;        // 最近一次有效完整帧。
    uint32_t last_event_us;   // 最近有效完整或重复事件的微秒时间戳。
    uint8_t state;            // 当前脉冲解析状态。
    uint8_t bit_count;        // 已接收的数据位数。
    uint8_t has_last;         // 最近完整帧是否仍可用于重复事件。
} NecDecoder;

/**
 * @brief 重置协议解析及重复帧历史。
 * @param decoder 解码器对象，空指针时不操作。
 */
void PortIr_DecoderInit(NecDecoder *decoder);

/**
 * @brief 送入一个已结束的脉冲，并校验完整帧末尾。
 * @param decoder 解码器对象。
 * @param level 脉冲电平0或1；2表示丢失，3表示计数器溢出时的空闲检查。
 * @param duration_us 脉冲微秒时长，使用32位避免长间隔被16位截断。
 * @param end_us 脉冲结束时的32位微秒计数，允许自然回绕。
 * @param event 有效事件输出；未产生事件时保持原值。
 * @return 产生事件为1，参数错误或未产生事件为0。
 */
uint8_t PortIr_FeedPulse(NecDecoder *decoder, uint8_t level,
                         uint32_t duration_us, uint32_t end_us, IrKeyEvent *event);

/**
 * @brief 查询库中的数字键映射。
 * @param command NEC命令字节。
 * @return 映射数字0至9，未知命令为-1。
 */
int8_t PortIr_CommandDigit(uint8_t command);

#endif
