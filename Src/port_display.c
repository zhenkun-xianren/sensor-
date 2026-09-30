#include "port_display.h"
#include "dev_display.h"
#include "bsp_display.h"

#include <stddef.h>

static const uint8_t s_digits[] = { // 十进制数字 0 至 9 对应的七段字形表。
    PORT_DISPLAY_GLYPH_0, PORT_DISPLAY_GLYPH_1, PORT_DISPLAY_GLYPH_2,
    PORT_DISPLAY_GLYPH_3, PORT_DISPLAY_GLYPH_4, PORT_DISPLAY_GLYPH_5,
    PORT_DISPLAY_GLYPH_6, PORT_DISPLAY_GLYPH_7, PORT_DISPLAY_GLYPH_8,
    PORT_DISPLAY_GLYPH_9
};

/**
 * @brief 将十进制数字转换为七段数码管字形。
 * @param digit 待编码的十进制数字。
 * @return 对应的七段数码管字形；数字越界时返回空白字形。
 */
uint8_t PortDisplay_Glyph(uint8_t digit)
{
    return digit < PORT_DISPLAY_DECIMAL_DIGIT_COUNT ? s_digits[digit] : PORT_DISPLAY_GLYPH_BLANK;
}

/**
 * @brief 在指定数码位显示一个十进制数字并按要求附加小数点。
 * @param index 要更新的数码位下标，按 DIG1 至 DIG4 对应 0 至 3。
 * @param digit 要显示的十进制数字。
 * @param decimal_point 非 0 表示点亮小数点，0 表示关闭小数点。
 */
void PortDisplay_ShowDigit(uint8_t index, uint8_t digit, uint8_t decimal_point)
{
    uint8_t glyph = PortDisplay_Glyph(digit); // 待输出数字对应的七段字形。

    if (decimal_point != 0U) glyph |= PORT_DISPLAY_GLYPH_DECIMAL_POINT;
    BSP_Display_SetDigit(index, glyph);
}

/**
 * @brief 将 0 至 9999 的整数编码为包含前导零的四位无小数点字形。
 * @param value 要显示的整数。
 * @param frame 接收 DIG1 至 DIG4 字形数据的缓冲区。
 */
void PortDisplay_EncodeInteger4(uint16_t value, uint8_t frame[PORT_DISPLAY_DIGIT_COUNT])
{
    uint8_t index; // 从个位向高位编码时使用的数码位索引。

    if (frame == NULL) {
        return;
    }
    if (value > PORT_DISPLAY_MAX_FIXED_POINT_VALUE) {
        PortDisplay_EncodeError(PORT_DISPLAY_ERROR_OUT_OF_RANGE, frame);
        return;
    }

    for (index = PORT_DISPLAY_DIGIT_COUNT; index > 0U; --index) {
        frame[index - 1U] = PortDisplay_Glyph((uint8_t)(value % 10U));
        value = (uint16_t)(value / 10U);
    }
}

/**
 * @brief 将放大后的定点数编码为四位数码管显示字形。
 * @param scaled_value 放大后的整数值，例如 1234 配合 1 位小数表示 123.4。
 * @param decimal_places 小数位数，取值范围为 0 到 3。
 * @param frame 接收 DIG1 至 DIG4 字形数据的缓冲区。
 */
void PortDisplay_EncodeFixedPoint(uint16_t scaled_value, uint8_t decimal_places,
                                  uint8_t frame[PORT_DISPLAY_DIGIT_COUNT])
{
    uint16_t integer_part; // 去除小数缩放后待编码的整数部分。
    uint16_t divisor; // 提取当前整数位数字所用的十进制除数。
    uint8_t decimal_index; // 小数点应点亮的数码位索引。
    uint8_t digit_index; // 遍历数码位时使用的索引。
    uint8_t digit; // 当前从整数部分提取的十进制数字。
    uint8_t has_significant_digit; // 标记是否已遇到首个非零整数位。

    if (frame == NULL) {
        return;
    }
    if (decimal_places > PORT_DISPLAY_MAX_DECIMAL_PLACES ||
        scaled_value > PORT_DISPLAY_MAX_FIXED_POINT_VALUE) {
        PortDisplay_EncodeError(PORT_DISPLAY_ERROR_OUT_OF_RANGE, frame);
        return;
    }

    for (digit_index = 0U; digit_index < PORT_DISPLAY_DIGIT_COUNT; ++digit_index) {
        frame[digit_index] = PORT_DISPLAY_GLYPH_BLANK;
    }

    /* 小数位从 DIG4 向左依次填充，始终保留用户指定的小数位数。 */
    integer_part = scaled_value;
    for (digit_index = PORT_DISPLAY_DIGIT_COUNT;
         digit_index > (uint8_t)(PORT_DISPLAY_DIGIT_COUNT - decimal_places);
         --digit_index) {
        uint8_t target_index = (uint8_t)(digit_index - 1U); // 从循环边界换算得到的目标数码位索引。
        frame[target_index] = PortDisplay_Glyph((uint8_t)(integer_part % 10U));
        integer_part = (uint16_t)(integer_part / 10U);
    }

    /* 小数点位于最后一个整数位上；没有小数位时不点亮 DP。 */
    decimal_index = (uint8_t)(PORT_DISPLAY_DIGIT_COUNT - decimal_places - 1U);
    divisor = 1U;
    for (digit_index = 0U; digit_index < decimal_index; ++digit_index) {
        divisor = (uint16_t)(divisor * 10U);
    }
    has_significant_digit = 0U;

    /* 整数部分从左向右处理，只有有效数字左侧的零才显示为空白。 */
    for (digit_index = 0U;
         digit_index <= decimal_index;
         ++digit_index) {
        digit = (uint8_t)((integer_part / divisor) % 10U);
        if (digit != 0U || has_significant_digit != 0U ||
            digit_index == decimal_index) {
            frame[digit_index] = PortDisplay_Glyph(digit);
            if (digit != 0U) {
                has_significant_digit = 1U;
            }
        }
        if (digit_index < decimal_index) {
            divisor = (uint16_t)(divisor / 10U);
        }
    }

    if (decimal_places > 0U) {
        frame[decimal_index] |= PORT_DISPLAY_GLYPH_DECIMAL_POINT;
    }
}

/**
 * @brief 将错误码编码为 E 标记和两位十进制数字。
 * @param code 要显示的错误码。
 * @param frame 接收四个数码位的字形数据。
 */
void PortDisplay_EncodeError(uint8_t code, uint8_t frame[PORT_DISPLAY_DIGIT_COUNT])
{
    frame[PORT_DISPLAY_DIGIT_1_INDEX] = PORT_DISPLAY_GLYPH_BLANK;
    frame[PORT_DISPLAY_DIGIT_2_INDEX] = PORT_DISPLAY_GLYPH_E;
    frame[PORT_DISPLAY_DIGIT_3_INDEX] = PortDisplay_Glyph((uint8_t)((code / 10U) % 10U));
    frame[PORT_DISPLAY_DIGIT_4_INDEX] = PortDisplay_Glyph((uint8_t)(code % 10U));
}

static DevDisplay s_display; // 绑定数码管 Port 实现的静态设备接口对象。

/**
 * @brief 初始化数码管显示设备。
 * @param self 数码管设备对象指针。
 * @return 显示引脚配置成功返回 1，否则返回 0。
 */
static uint8_t display_init(DevDisplay *self)
{
    (void)self;
    return BSP_Display_Init();
}

/**
 * @brief 执行一次数码管扫描并切换到下一位。
 * @param self 数码管设备对象指针。
 */
static void display_scan_step(DevDisplay *self)
{
    (void)self;
    BSP_Display_ScanStep();
}

/**
 * @brief 清空所有数码位的显示字形。
 * @param self 数码管设备对象指针。
 */
static void display_clear(DevDisplay *self)
{
    static const uint8_t blank[PORT_DISPLAY_DIGIT_COUNT] = {0U, 0U, 0U, 0U}; // 清屏时写入各位的空白字形。
    (void)self;
    BSP_Display_SetFrame(blank);
}

/**
 * @brief 在指定数码位显示数字并按要求控制小数点。
 * @param self 数码管设备对象指针。
 * @param position 要显示的数码位，使用 DEV_DISPLAY_DIG1 至 DEV_DISPLAY_DIG4。
 * @param digit 要显示的十进制数字。
 * @param decimal_point 小数点状态，使用 DEV_DISPLAY_DECIMAL_POINT_OFF 或 ON。
 */
static void display_show_digit(DevDisplay *self, DevDisplayPosition position,
                               uint8_t digit,
                               DevDisplayDecimalPoint decimal_point)
{
    (void)self;
    PortDisplay_ShowDigit((uint8_t)position, digit, (uint8_t)decimal_point);
}

/**
 * @brief 在四位数码管上显示无小数点的整数。
 * @param self 数码管设备对象指针。
 * @param value 要显示的整数，范围为 0 至 9999。
 */
static void display_show_integer4(DevDisplay *self, uint16_t value)
{
    uint8_t frame[PORT_DISPLAY_DIGIT_COUNT]; // 保存编码后四位数码管字形的临时缓冲区。
    (void)self;
    PortDisplay_EncodeInteger4(value, frame);
    BSP_Display_SetFrame(frame);
}

/**
 * @brief 在四位数码管上显示指定小数位数的定点数值。
 * @param self 数码管设备对象指针。
 * @param scaled_value 放大后的整数值。
 * @param decimal_places 小数位数，取值范围为 0 到 3。
 */
static void display_show_fixed_point(DevDisplay *self, uint16_t scaled_value,
                                     uint8_t decimal_places)
{
    uint8_t frame[PORT_DISPLAY_DIGIT_COUNT]; // 保存编码后四位数码管字形的临时缓冲区。
    (void)self;
    PortDisplay_EncodeFixedPoint(scaled_value, decimal_places, frame);
    BSP_Display_SetFrame(frame);
}

/**
 * @brief 显示错误标记及其数字码。
 * @param self 数码管设备对象指针。
 * @param code 要显示的错误码。
 */
static void display_show_error(DevDisplay *self, uint8_t code)
{
    uint8_t frame[PORT_DISPLAY_DIGIT_COUNT]; // 保存错误码对应四位字形的临时缓冲区。
    (void)self;
    PortDisplay_EncodeError(code, frame);
    BSP_Display_SetFrame(frame);
}

/**
 * @brief 点亮所有数码位上的全部段选信号。
 * @param self 数码管设备对象指针。
 */
static void display_show_all(DevDisplay *self)
{
    static const uint8_t all[PORT_DISPLAY_DIGIT_COUNT] = { // 全段点亮时写入各数码位的字形。
        PORT_DISPLAY_GLYPH_ALL_SEGMENTS,
        PORT_DISPLAY_GLYPH_ALL_SEGMENTS,
        PORT_DISPLAY_GLYPH_ALL_SEGMENTS,
        PORT_DISPLAY_GLYPH_ALL_SEGMENTS
    };
    (void)self;
    BSP_Display_SetFrame(all);
}

/**
 * @brief 获取数码管显示设备接口并绑定 Port 实现。
 * @return 指向静态数码管设备接口的指针。
 */
DevDisplay *GetDisplay(void)
{
    s_display.init = display_init;
    s_display.scanStep = display_scan_step;
    s_display.clear = display_clear;
    s_display.showDigit = display_show_digit;
    s_display.showInteger4 = display_show_integer4;
    s_display.showFixedPoint = display_show_fixed_point;
    s_display.showError = display_show_error;
    s_display.showAll = display_show_all;
    return &s_display;
}
