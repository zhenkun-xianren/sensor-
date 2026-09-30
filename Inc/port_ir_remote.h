#ifndef PORT_IR_REMOTE_H
#define PORT_IR_REMOTE_H

#include <stdint.h>
#include "dev_ir_remote.h"

#define PORT_IR_NEC_FRAME_BITS                         32U
#define PORT_IR_NEC_FRAME_TIMEOUT_US              15000U
#define PORT_IR_NEC_LEADER_MARK_MIN_US              7000U
#define PORT_IR_NEC_LEADER_MARK_MAX_US             11000U
#define PORT_IR_NEC_LEADER_SPACE_MIN_US             3000U
#define PORT_IR_NEC_LEADER_SPACE_MAX_US             6000U
#define PORT_IR_NEC_REPEAT_SPACE_MIN_US             1500U
#define PORT_IR_NEC_REPEAT_SPACE_MAX_US             2800U
#define PORT_IR_NEC_BIT_MARK_MIN_US                  300U
#define PORT_IR_NEC_BIT_MARK_MAX_US                 1000U
#define PORT_IR_NEC_ZERO_SPACE_MIN_US                 300U
#define PORT_IR_NEC_ZERO_SPACE_MAX_US                1000U
#define PORT_IR_NEC_ONE_SPACE_MIN_US                 1200U
#define PORT_IR_NEC_ONE_SPACE_MAX_US                 2500U
#define PORT_IR_NEC_BYTE_WIDTH_BITS                     8U
#define PORT_IR_NEC_ADDRESS_HIGH_OFFSET_BITS            8U
#define PORT_IR_NEC_COMMAND_OFFSET_BITS                16U
#define PORT_IR_NEC_COMMAND_INVERSE_OFFSET_BITS        24U
#define PORT_IR_NEC_BYTE_MASK                         0xFFU

/** @brief NEC 遥控器数字 0 至 9 对应的命令码。 */
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

/**
 * @brief 保存 NEC 解码器的当前帧和最近有效帧状态。
 */
typedef struct {
    uint32_t raw;       /**< 当前正在接收的 32 位帧数据。 */
    uint32_t last_raw;  /**< 最近一次完整且校验有效的帧数据。 */
    uint8_t state;      /**< 当前协议解析状态。 */
    uint8_t bit_count;  /**< 当前帧已接收的数据位数，范围为 0 至 32。 */
    uint8_t has_last;   /**< 非零表示 last_raw 可用于解析 NEC 重复帧。 */
} NecDecoder;

/**
 * @brief 重置 NEC 解码状态和未完成的帧数据。
 * @param decoder 要初始化的解码器状态。
 */
void PortIr_DecoderInit(NecDecoder *decoder);

/**
 * @brief 将一个已测时的红外脉冲送入 NEC 帧解码器。
 * @param decoder 要更新的解码器状态。
 * @param level 脉冲期间检测到的逻辑电平。
 * @param duration_us 脉冲时长，单位为微秒。
 * @param event 找到完整有效帧时接收解码后的事件。
 * @return 解码出按键事件返回 1，否则返回 0。
 */
uint8_t PortIr_FeedPulse(NecDecoder *decoder, uint8_t level,
                         uint16_t duration_us, IrKeyEvent *event);

/**
 * @brief 将 NEC 遥控命令字节映射为十进制数字。
 * @param command 遥控器发送的 NEC 命令字节。
 * @return 命令对应的数字 0 至 9；非数字命令返回 -1。
 */
int8_t PortIr_CommandDigit(uint8_t command);

/**
 * @brief 在中断中解码红外脉冲，并将完成的 NEC 按键事件加入队列。
 * @param level 脉冲期间检测到的逻辑电平。
 * @param duration_us 脉冲时长，单位为微秒。
 */
void PortIr_OnPulseIsr(uint8_t level, uint16_t duration_us);

#endif /* PORT_IR_REMOTE_H */
