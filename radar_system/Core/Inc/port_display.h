#ifndef PORT_DISPLAY_H
#define PORT_DISPLAY_H

#include <stdint.h>
#include "dev_display.h"

#define PORT_DISPLAY_DIGIT_COUNT DISPLAY_CONFIG_DIGIT_COUNT
#define PORT_DISPLAY_DECIMAL_DIGIT_COUNT 10U
#define PORT_DISPLAY_DECIMAL_ZERO 0U // 十进制零值，用于前导零判断。
#define PORT_DISPLAY_MAX_DECIMAL_PLACES DISPLAY_CONFIG_MAX_DECIMAL_PLACES
#define PORT_DISPLAY_MAX_FIXED_POINT_VALUE DISPLAY_CONFIG_MAX_VALUE
#define PORT_DISPLAY_ERROR_OUT_OF_RANGE 4U
#define PORT_DISPLAY_NO_DECIMAL_PLACES 0U
#define PORT_DISPLAY_DECIMAL_DIVISOR_INITIAL 1U
#define PORT_DISPLAY_ERROR_CODE_MODULUS 100U
#define PORT_DISPLAY_ERROR_FRAME_DIGITS 3U
#define PORT_DISPLAY_ERROR_PREFIX_INDEX (PORT_DISPLAY_DIGIT_COUNT - PORT_DISPLAY_ERROR_FRAME_DIGITS)
#define PORT_DISPLAY_ERROR_TENS_INDEX (PORT_DISPLAY_ERROR_PREFIX_INDEX + 1U)
#define PORT_DISPLAY_ERROR_UNITS_INDEX (PORT_DISPLAY_ERROR_PREFIX_INDEX + 2U)
#define PORT_DISPLAY_GLYPH_ALL_SEGMENTS 0xFFU

enum {
    PORT_DISPLAY_DIGIT_1_INDEX = 0U,
    PORT_DISPLAY_DIGIT_2_INDEX = 1U,
    PORT_DISPLAY_DIGIT_3_INDEX = 2U,
#if DISPLAY_CONFIG_DIGIT_COUNT == DISPLAY_CONFIG_FOUR_DIGITS
    PORT_DISPLAY_DIGIT_4_INDEX = 3U,
#endif
    PORT_DISPLAY_GLYPH_BLANK = 0x00U,
    PORT_DISPLAY_GLYPH_DASH = 0x40U, // 仅点亮G段形成横线。
    PORT_DISPLAY_GLYPH_E = 0x79U,
    PORT_DISPLAY_GLYPH_DECIMAL_POINT = 0x80U,
    PORT_DISPLAY_GLYPH_0 = 0x3FU,
    PORT_DISPLAY_GLYPH_1 = 0x06U,
    PORT_DISPLAY_GLYPH_2 = 0x5BU,
    PORT_DISPLAY_GLYPH_3 = 0x4FU,
    PORT_DISPLAY_GLYPH_4 = 0x66U,
    PORT_DISPLAY_GLYPH_5 = 0x6DU,
    PORT_DISPLAY_GLYPH_6 = 0x7DU,
    PORT_DISPLAY_GLYPH_7 = 0x07U,
    PORT_DISPLAY_GLYPH_8 = 0x7FU,
    PORT_DISPLAY_GLYPH_9 = 0x6FU
};

/**
 * @brief 将定点数编码为配置位数的七段字形帧。
 * @param scaled_value 按小数位放大后的数值，三位为0至999，四位为0至9999。
 * @param decimal_places 小数位数，三位为0至2，四位为0至3。
 * @param mode 整数前导零模式，SUPPRESS显示空白，PAD补零。
 * @param frame 接收当前配置位数字形的输出缓冲区，至少包含 PORT_DISPLAY_DIGIT_COUNT 个字节。
 * @details 数值或小数位越界编码为E04；空缓冲区或非法模式不写入缓冲区。
 */
void PortDisplay_EncodeFixedPoint(uint16_t scaled_value, uint8_t decimal_places,
                                  DevDisplayLeadingZeroMode mode,
                                  uint8_t frame[PORT_DISPLAY_DIGIT_COUNT]);

#if DISPLAY_CONFIG_DIGIT_COUNT == DISPLAY_CONFIG_FOUR_DIGITS
/**
 * @brief 将 0 至 9999 的整数编码为无小数点的四位字形帧。
 * @param value 要显示的整数，范围为 0 至 9999。
 * @param frame 接收当前配置位数字形的输出缓冲区，至少包含 PORT_DISPLAY_DIGIT_COUNT 个字节。
 */
void PortDisplay_EncodeInteger4(uint16_t value,
                                uint8_t frame[PORT_DISPLAY_DIGIT_COUNT]);
#endif


/**
 * @brief 将错误码编码为E和末两位数字；四位模式额外在左侧留空。
 * @param code 要显示的错误码，显示其十进制末两位。
 * @param frame 接收当前配置位数字形的输出缓冲区，至少包含 PORT_DISPLAY_DIGIT_COUNT 个字节。
 */
void PortDisplay_EncodeError(uint8_t code, uint8_t frame[PORT_DISPLAY_DIGIT_COUNT]);

/**
 * @brief 获取十进制数字对应的七段字形。
 * @param digit 要转换的数字，范围为 0 至 9。
 * @return 对应字形；数字超出范围时返回空白字形。
 */
uint8_t PortDisplay_Glyph(uint8_t digit);

#endif
