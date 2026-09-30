#ifndef PORT_IR_REMOTE_H
#define PORT_IR_REMOTE_H

#include <stdint.h>
#include "dev_ir_remote.h"

#define PORT_IR_NEC_FRAME_BITS 32U
#define PORT_IR_NEC_FRAME_TIMEOUT_US 15000U
#define PORT_IR_NEC_LEADER_MARK_MIN_US 7000U
#define PORT_IR_NEC_LEADER_MARK_MAX_US 11000U
#define PORT_IR_NEC_LEADER_SPACE_MIN_US 3000U
#define PORT_IR_NEC_LEADER_SPACE_MAX_US 6000U
#define PORT_IR_NEC_REPEAT_SPACE_MIN_US 1500U
#define PORT_IR_NEC_REPEAT_SPACE_MAX_US 2800U
#define PORT_IR_NEC_BIT_MARK_MIN_US 300U
#define PORT_IR_NEC_BIT_MARK_MAX_US 1000U
#define PORT_IR_NEC_ZERO_SPACE_MIN_US 300U
#define PORT_IR_NEC_ZERO_SPACE_MAX_US 1000U
#define PORT_IR_NEC_ONE_SPACE_MIN_US 1200U
#define PORT_IR_NEC_ONE_SPACE_MAX_US 2500U
#define PORT_IR_NEC_BYTE_WIDTH_BITS 8U
#define PORT_IR_NEC_ADDRESS_HIGH_OFFSET_BITS 8U
#define PORT_IR_NEC_COMMAND_OFFSET_BITS 16U
#define PORT_IR_NEC_COMMAND_INVERSE_OFFSET_BITS 24U
#define PORT_IR_NEC_BYTE_MASK 0xFFU

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

typedef struct {
    uint32_t raw;
    uint32_t last_raw;
    uint8_t state;
    uint8_t bit_count;
    uint8_t has_last;
} NecDecoder;

void PortIr_DecoderInit(NecDecoder *decoder);
uint8_t PortIr_FeedPulse(NecDecoder *decoder, uint8_t level, uint16_t duration_us,
                         IrKeyEvent *event);
int8_t PortIr_CommandDigit(uint8_t command);
void PortIr_OnPulseIsr(uint8_t level, uint16_t duration_us);

#endif
