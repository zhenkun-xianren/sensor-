#ifndef PORT_IR_REMOTE_H
#define PORT_IR_REMOTE_H
#include "dev_ir_remote.h"
#define PORT_IR_NEC_FRAME_BITS 32U
#define PORT_IR_NEC_FRAME_TIMEOUT_US 15000UL
#define PORT_IR_NEC_REPEAT_VALID_US 150000UL
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
#define PORT_IR_NEC_BYTE_BITS 8U
#define PORT_IR_NEC_COMMAND_SHIFT 16U
#define PORT_IR_NEC_INVERSE_SHIFT 24U
#define PORT_IR_NEC_BYTE_MASK 0xFFU
#define PORT_IR_QUEUE_SIZE 8U
/** @brief 单个 NEC 接收通道的协议状态。 */
typedef struct {
    uint32_t raw; // 正在拼接的最低位先行帧。
    uint32_t last_raw; // 最近一次有效帧的原始数据。
    uint32_t last_time_us; // 最近有效帧或重复帧完成的微秒时间戳。
    uint8_t state; // 当前脉冲解析状态。
    uint8_t bit_count; // 当前已接收的数据位数。
    uint8_t has_last; // 是否保存了可供重复帧使用的完整帧。
} NecDecoder;
/**
 * @brief 清除 NEC 解码状态。
 * @param decoder 待初始化的非空解码器。
 */
void PortIr_DecoderInit(NecDecoder *decoder);
/**
 * @brief 处理已结束的一个电平脉冲。
 * @param decoder 保存协议状态的解码器。
 * @param level 脉冲期间的电平，0为低电平。
 * @param duration_us 脉冲长度，单位微秒。
 * @param now_us 脉冲结束时的32位微秒时间戳，允许自然回绕。
 * @param event 有效完整帧或重复帧的输出。
 * @return 得到有效按键返回1，继续等待或无效脉冲返回0。
 */
uint8_t PortIr_FeedPulse(NecDecoder *decoder, uint8_t level,
                        uint32_t duration_us, uint32_t now_us, IrKeyEvent *event);
extern volatile uint32_t g_port_ir_dropped; // 环形缓冲满时丢弃的按键事件数量。
#endif
