#ifndef PORT_DISPLAY_H
#define PORT_DISPLAY_H

#include <stdint.h>

#define PORT_DISPLAY_DIGIT_COUNT 4U
#define PORT_DISPLAY_DECIMAL_DIGIT_COUNT 10U
#define PORT_DISPLAY_MAX_DECIMAL_PLACES 3U
#define PORT_DISPLAY_MAX_FIXED_POINT_VALUE 9999U
#define PORT_DISPLAY_ERROR_OUT_OF_RANGE 4U
#define PORT_DISPLAY_GLYPH_ALL_SEGMENTS 0xFFU

enum {
    PORT_DISPLAY_DIGIT_1_INDEX = 0U,
    PORT_DISPLAY_DIGIT_2_INDEX = 1U,
    PORT_DISPLAY_DIGIT_3_INDEX = 2U,
    PORT_DISPLAY_DIGIT_4_INDEX = 3U,
    PORT_DISPLAY_GLYPH_BLANK = 0x00U,
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
 * @brief 在指定数码位显示数字，并按需设置小数点。
 * @param index 数码位索引，0 至 3 依次对应 DIG1 至 DIG4。
 * @param digit 要显示的数字；范围外的值显示为空白。
 * @param decimal_point 非零表示点亮小数点，零表示关闭。
 */
void PortDisplay_ShowDigit(uint8_t index, uint8_t digit, uint8_t decimal_point);

/**
 * @brief 将定点数编码为 DIG1 至 DIG4 的七段字形帧。
 * @param scaled_value 按小数位放大后的数值，范围为 0 至 9999。
 * @param decimal_places 小数位数，范围为 0 至 3。
 * @param frame 接收四个字形的输出缓冲区，至少包含 PORT_DISPLAY_DIGIT_COUNT 个字节。
 */
void PortDisplay_EncodeFixedPoint(uint16_t scaled_value, uint8_t decimal_places,
                                  uint8_t frame[PORT_DISPLAY_DIGIT_COUNT]);

/**
 * @brief 将 0 至 9999 的整数编码为无小数点的四位字形帧。
 * @param value 要显示的整数，范围为 0 至 9999。
 * @param frame 接收四个字形的输出缓冲区，至少包含 PORT_DISPLAY_DIGIT_COUNT 个字节。
 */
void PortDisplay_EncodeInteger4(uint16_t value,
                                uint8_t frame[PORT_DISPLAY_DIGIT_COUNT]);

/**
 * @brief 将错误码编码为左侧空白、E 和错误码末两位的字形帧。
 * @param code 要显示的错误码，显示其十进制末两位。
 * @param frame 接收四个字形的输出缓冲区，至少包含 PORT_DISPLAY_DIGIT_COUNT 个字节。
 */
void PortDisplay_EncodeError(uint8_t code, uint8_t frame[PORT_DISPLAY_DIGIT_COUNT]);

/**
 * @brief 获取十进制数字对应的七段字形。
 * @param digit 要转换的数字，范围为 0 至 9。
 * @return 对应字形；数字超出范围时返回空白字形。
 */
uint8_t PortDisplay_Glyph(uint8_t digit);

#endif
