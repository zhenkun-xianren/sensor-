/* 三／四位可配置数码管通用编码、设备状态及扫描缓冲；板级操作通过BSP契约提供。 */
#include "port_display.h"
#include "dev_display.h"
#include "bsp_display.h"

#include <stddef.h>
#include <stdbool.h>

static const uint8_t s_digits[] = { // 数字0至9的A至G高有效字形表。
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

#if DISPLAY_CONFIG_DIGIT_COUNT == DISPLAY_CONFIG_FOUR_DIGITS
/**
 * @brief 将 0 至 9999 的整数编码为包含前导零的四位无小数点字形。
 * @param value 要显示的整数。
 * @param frame 接收当前配置全部数码位字形的缓冲区。
 */
void PortDisplay_EncodeInteger4(uint16_t value, uint8_t frame[PORT_DISPLAY_DIGIT_COUNT])
{
    PortDisplay_EncodeFixedPoint(value, PORT_DISPLAY_NO_DECIMAL_PLACES, DEV_DISPLAY_LEADING_ZERO_PAD, frame);
}
#endif


/**
 * @brief 将放大后的定点数编码为配置位数的数码管字形。
 * @param scaled_value 放大后的整数值，例如123配合1位小数表示12.3。
 * @param decimal_places 小数位数，三位0至2，四位0至3。
 * @param mode 整数前导零模式，SUPPRESS抑制，PAD补齐。
 * @param frame 接收当前配置全部数码位字形的缓冲区。
 * @details 数值或小数位越界编码为E04；空缓冲区或非法模式保持缓冲区不变。
 */
void PortDisplay_EncodeFixedPoint(uint16_t scaled_value, uint8_t decimal_places,
                                  DevDisplayLeadingZeroMode mode,
                                  uint8_t frame[PORT_DISPLAY_DIGIT_COUNT])
{
    uint16_t integer_part; // 提取小数位后剩余的整数部分。
    uint16_t divisor; // 从高位到低位提取十进制数字的除数。
    uint8_t decimal_index; // 最后一个整数位及小数点所在的位置。
    uint8_t digit_index; // 遍历数码位的索引。
    uint8_t digit; // 当前提取的十进制数字。
    bool has_significant_digit; // 记录整数部分是否已经遇到有效数字。

    if (frame == NULL ||
        ((mode != DEV_DISPLAY_LEADING_ZERO_SUPPRESS) &&
         (mode != DEV_DISPLAY_LEADING_ZERO_PAD))) {
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

    /* 小数位从最右位向左依次填充，始终保留用户指定的小数位数。 */
    integer_part = scaled_value;
    for (digit_index = PORT_DISPLAY_DIGIT_COUNT;
         digit_index > (uint8_t)(PORT_DISPLAY_DIGIT_COUNT - decimal_places);
         --digit_index) {
        uint8_t target_index = (uint8_t)(digit_index - 1U); // 正在填充的小数位位置。
        frame[target_index] = PortDisplay_Glyph((uint8_t)(integer_part % PORT_DISPLAY_DECIMAL_DIGIT_COUNT));
        integer_part = (uint16_t)(integer_part / PORT_DISPLAY_DECIMAL_DIGIT_COUNT);
    }

    /* 小数点位于最后一个整数位上；没有小数位时不点亮 DP。 */
    decimal_index = (uint8_t)(PORT_DISPLAY_DIGIT_COUNT - decimal_places - 1U);
    divisor = PORT_DISPLAY_DECIMAL_DIVISOR_INITIAL;
    for (digit_index = 0U; digit_index < decimal_index; ++digit_index) {
        divisor = (uint16_t)(divisor * PORT_DISPLAY_DECIMAL_DIGIT_COUNT);
    }
    has_significant_digit = false;

    /* 同一循环完成两种格式；抑制模式将有效数字左侧留空，补零模式填满整数区域。 */
    for (digit_index = 0U;
         digit_index <= decimal_index;
         ++digit_index) {
        digit = (uint8_t)((integer_part / divisor) % PORT_DISPLAY_DECIMAL_DIGIT_COUNT);
        if (mode == DEV_DISPLAY_LEADING_ZERO_PAD || digit != PORT_DISPLAY_DECIMAL_ZERO || has_significant_digit ||
            digit_index == decimal_index) {
            frame[digit_index] = PortDisplay_Glyph(digit);
            if (digit != PORT_DISPLAY_DECIMAL_ZERO) {
                has_significant_digit = true;
            }
        }
        if (digit_index < decimal_index) {
            divisor = (uint16_t)(divisor / PORT_DISPLAY_DECIMAL_DIGIT_COUNT);
        }
    }

    if (decimal_places != PORT_DISPLAY_NO_DECIMAL_PLACES) {
        frame[decimal_index] |= PORT_DISPLAY_GLYPH_DECIMAL_POINT;
    }
}

/**
 * @brief 将错误码编码为 E 标记和两位十进制数字。
 * @param code 要显示的错误码。
 * @param frame 接收当前配置全部数码位的字形数据。
 */
void PortDisplay_EncodeError(uint8_t code, uint8_t frame[PORT_DISPLAY_DIGIT_COUNT])
{
    uint8_t index; // 遍历当前配置的所有数码位，四位模式首位留空。
    uint8_t display_code = (uint8_t)(code % PORT_DISPLAY_ERROR_CODE_MODULUS); // 错误码仅显示末两位。
    if (frame == NULL) {
        return;
    }
    for (index = 0U; index < PORT_DISPLAY_DIGIT_COUNT; ++index) {
        frame[index] = PORT_DISPLAY_GLYPH_BLANK;
    }
    frame[PORT_DISPLAY_ERROR_PREFIX_INDEX] = PORT_DISPLAY_GLYPH_E;
    frame[PORT_DISPLAY_ERROR_TENS_INDEX] = PortDisplay_Glyph((uint8_t)(display_code / PORT_DISPLAY_DECIMAL_DIGIT_COUNT));
    frame[PORT_DISPLAY_ERROR_UNITS_INDEX] = PortDisplay_Glyph((uint8_t)(display_code % PORT_DISPLAY_DECIMAL_DIGIT_COUNT));
}


static DevDisplay s_display; // 本Port的唯一静态设备接口对象。
static uint8_t s_pending[PORT_DISPLAY_DIGIT_COUNT]; // 最近提交的完整字形帧。
static uint8_t s_active[PORT_DISPLAY_DIGIT_COUNT]; // 当前完整扫描周期使用的一致帧。
static uint8_t s_current; // 下一次输出的数码位，范围0至配置位数减一。
static bool s_initialized; // BSP初始化成功标志。
static bool s_enabled; // 输出允许标志，stop后扫描不得重新点亮。
static bool s_dirty; // 最近提交帧尚未在DIG1边界应用的标志。

/**
 * @brief 在临界区内检查设备对象及初始化状态。
 * @param self 调用方法的设备对象。
 * @return 对象无效返回INVALID_ARGUMENT，未初始化返回NOT_INITIALIZED，否则返回OK。
 */
static DevDisplayStatus display_check(DevDisplay *self)
{
    if (self != &s_display) {
        return DEV_DISPLAY_INVALID_ARGUMENT;
    }
    return (s_initialized) ? DEV_DISPLAY_OK : DEV_DISPLAY_NOT_INITIALIZED;
}

/**
 * @brief 原子提交完整帧，保留当前扫描周期，下一次DIG1才切换新内容。
 * @param self 数码管设备对象。
 * @param frame 待提交的完整字形帧，内部调用保证非空。
 * @return 成功返回OK，对象无效或未初始化返回对应错误状态。
 */
static DevDisplayStatus display_submit(DevDisplay *self, const uint8_t frame[PORT_DISPLAY_DIGIT_COUNT])
{
    uint32_t saved_mask = BSP_Display_EnterCritical(); // 保存中断状态以保护帧和扫描状态。
    DevDisplayStatus result = display_check(self); // 提交时重新检查，避免与重新初始化竞争。
    uint8_t index; // 完整帧复制时的数码位索引。
    if (result == DEV_DISPLAY_OK) {
        for (index = 0U; index < PORT_DISPLAY_DIGIT_COUNT; ++index) {
            s_pending[index] = frame[index];
        }
        s_dirty = true;
        s_enabled = true;
    }
    BSP_Display_ExitCritical(saved_mask);
    return result;
}

/**
 * @brief 检查对象后初始化BSP，清空所有帧和扫描状态；失败不得继续输出。
 * @param self 数码管设备对象。
 * @return 成功返回OK，对象无效返回INVALID_ARGUMENT，BSP失败返回HARDWARE_ERROR。
 */
static DevDisplayStatus display_init(DevDisplay *self)
{
    uint32_t saved_mask; // 保存整个初始化期间的中断状态。
    uint8_t index; // 清空两个显示缓冲区的数码位索引。
    int result; // 保存实际BSP初始化结果。
    if (self != &s_display) {
        return DEV_DISPLAY_INVALID_ARGUMENT;
    }
    saved_mask = BSP_Display_EnterCritical();
    s_initialized = false;
    s_enabled = false;
    s_current = PORT_DISPLAY_DIGIT_1_INDEX;
    s_dirty = false;
    for (index = 0U; index < PORT_DISPLAY_DIGIT_COUNT; ++index) {
        s_pending[index] = PORT_DISPLAY_GLYPH_BLANK;
        s_active[index] = PORT_DISPLAY_GLYPH_BLANK;
    }
    result = BSP_Display_Init();
    if (result == BSP_DISPLAY_OK) {
        s_initialized = true;
        s_enabled = true;
    }
    BSP_Display_ExitCritical(saved_mask);
    return (result == BSP_DISPLAY_OK) ? DEV_DISPLAY_OK : DEV_DISPLAY_HARDWARE_ERROR;
}

/**
 * @brief 使用同一帧扫描配置的全部数码位，仅在DIG1边界复制最新提交帧，避免新旧内容混合。
 * @param self 数码管设备对象。
 * @return 正常或停止返回OK，对象无效/未初始化返回对应状态，BSP输出失败返回HARDWARE_ERROR。
 */
static DevDisplayStatus display_scan_step(DevDisplay *self)
{
    uint32_t saved_mask = BSP_Display_EnterCritical(); // 保护状态、帧切换和完整GPIO输出顺序。
    DevDisplayStatus result = display_check(self); // 本次扫描调用的状态。
    uint8_t index; // 扫描边界复制帧时的数码位索引。
    if ((result == DEV_DISPLAY_OK) && (s_enabled)) {
        if ((s_current == PORT_DISPLAY_DIGIT_1_INDEX) && (s_dirty)) {
            for (index = 0U; index < PORT_DISPLAY_DIGIT_COUNT; ++index) {
                s_active[index] = s_pending[index];
            }
            s_dirty = false;
        }
        if (BSP_Display_Output(s_current, s_active[s_current]) != BSP_DISPLAY_OK) {
            BSP_Display_Blank();
            s_enabled = false;
            s_initialized = false;
            result = DEV_DISPLAY_HARDWARE_ERROR;
        } else {
            ++s_current;
            if (s_current >= PORT_DISPLAY_DIGIT_COUNT) {
                s_current = PORT_DISPLAY_DIGIT_1_INDEX;
            }
        }
    }
    BSP_Display_ExitCritical(saved_mask);
    return result;
}

/**
 * @brief 提交空白帧，保持普通扫描语义；需要立即关闭输出时调用stop。
 * @param self 数码管设备对象。
 * @return 成功返回OK，否则返回对象无效或未初始化状态。
 */
static DevDisplayStatus display_clear(DevDisplay *self)
{
    static const uint8_t blank[PORT_DISPLAY_DIGIT_COUNT] = {PORT_DISPLAY_GLYPH_BLANK}; // 按配置位数零初始化的空白帧。
    return display_submit(self, blank);
}

/**
 * @brief 原子更新最近提交帧中的一个数码位，非法参数保持全部状态不变。
 * @param self 数码管设备对象。
 * @param position 位置，三位DIG1至DIG3，四位DIG1至DIG4。
 * @param digit 数字，0至9。
 * @param decimal_point 小数点，OFF或ON。
 * @return 成功返回OK，参数无效返回INVALID_ARGUMENT，未初始化返回NOT_INITIALIZED。
 */
static DevDisplayStatus display_show_digit(DevDisplay *self, DevDisplayPosition position,
                                          uint8_t digit, DevDisplayDecimalPoint decimal_point)
{
    uint32_t saved_mask; // 单位更新期间的中断屏蔽状态。
    DevDisplayStatus result; // 设备对象和初始化检查结果。
    uint8_t glyph; // 当前数字及小数点的字形。
    if (((uint32_t)position >= PORT_DISPLAY_DIGIT_COUNT) ||
        (digit >= PORT_DISPLAY_DECIMAL_DIGIT_COUNT) ||
        ((decimal_point != DEV_DISPLAY_DECIMAL_POINT_OFF) &&
         (decimal_point != DEV_DISPLAY_DECIMAL_POINT_ON))) {
        return DEV_DISPLAY_INVALID_ARGUMENT;
    }
    glyph = PortDisplay_Glyph(digit);
    if (decimal_point == DEV_DISPLAY_DECIMAL_POINT_ON) {
        glyph |= PORT_DISPLAY_GLYPH_DECIMAL_POINT;
    }
    saved_mask = BSP_Display_EnterCritical();
    result = display_check(self);
    if (result == DEV_DISPLAY_OK) {
        s_pending[(uint8_t)position] = glyph;
        s_dirty = true;
        s_enabled = true;
    }
    BSP_Display_ExitCritical(saved_mask);
    return result;
}

/**
 * @brief 按模式编码并提交定点数，非法模式保持原显示，超范围提交E04。
 * @param self 数码管设备对象。
 * @param scaled_value 放大后整数，三位0至999，四位0至9999。
 * @param decimal_places 小数位数，三位0至2，四位0至3。
 * @param mode 整数前导零模式，SUPPRESS抑制，PAD补齐。
 * @return 模式无效返回INVALID_ARGUMENT，提交失败返回对象或初始化错误，超范围返回OUT_OF_RANGE，否则返回OK。
 */
static DevDisplayStatus display_show_fixed_point(DevDisplay *self, uint16_t scaled_value,
                                                uint8_t decimal_places,
                                                DevDisplayLeadingZeroMode mode)
{
    uint8_t frame[PORT_DISPLAY_DIGIT_COUNT]; // 本次定点数编码的完整帧。
    DevDisplayStatus result; // 原子提交结果。
    if ((mode != DEV_DISPLAY_LEADING_ZERO_SUPPRESS) &&
        (mode != DEV_DISPLAY_LEADING_ZERO_PAD)) {
        return DEV_DISPLAY_INVALID_ARGUMENT;
    }
    PortDisplay_EncodeFixedPoint(scaled_value, decimal_places, mode, frame);
    result = display_submit(self, frame);
    return ((result == DEV_DISPLAY_OK) &&
            ((scaled_value > PORT_DISPLAY_MAX_FIXED_POINT_VALUE) ||
             (decimal_places > PORT_DISPLAY_MAX_DECIMAL_PLACES))) ? DEV_DISPLAY_OUT_OF_RANGE : result;
}

#if DISPLAY_CONFIG_DIGIT_COUNT == DISPLAY_CONFIG_FOUR_DIGITS
/**
 * @brief 通过统一定点数方法显示四位补零整数，复用编码、范围处理和提交。
 * @param self 数码管设备对象。
 * @param value 待显示整数，范围为0至9999。
 * @return 提交失败返回对象或初始化错误，超范围返回OUT_OF_RANGE，否则返回OK。
 */
static DevDisplayStatus display_show_integer4(DevDisplay *self, uint16_t value)
{
    return display_show_fixed_point(self, value, PORT_DISPLAY_NO_DECIMAL_PLACES, DEV_DISPLAY_LEADING_ZERO_PAD);
}
#endif


/**
 * @brief 显示E和错误码末两位；四位模式首位空白。
 * @param self 数码管设备对象。
 * @param code 错误码，显示其模100的两位数字。
 * @return 成功返回OK，否则返回对象或初始化错误。
 */
static DevDisplayStatus display_show_error(DevDisplay *self, uint8_t code)
{
    uint8_t frame[PORT_DISPLAY_DIGIT_COUNT]; // 本次错误码编码的完整帧。
    PortDisplay_EncodeError(code, frame);
    return display_submit(self, frame);
}

/**
 * @brief 提交当前配置全部数码位的全段和小数点字形，扫描时仍只选通一位。
 * @param self 数码管设备对象。
 * @return 成功返回OK，否则返回对象或初始化错误。
 */
static DevDisplayStatus display_show_all(DevDisplay *self)
{
    uint8_t all[PORT_DISPLAY_DIGIT_COUNT]; // 当前配置位数的全段及DP帧。
    uint8_t index; // 填充每个实际数码位的索引。
    for (index = 0U; index < PORT_DISPLAY_DIGIT_COUNT; ++index) {
        all[index] = PORT_DISPLAY_GLYPH_ALL_SEGMENTS;
    }
    return display_submit(self, all);
}

/**
 * @brief 立即关闭物理输出并复位扫描位置，保留最后提交帧供后续内容更新。
 * @param self 数码管设备对象。
 * @return 成功返回OK，否则返回对象或初始化错误。
 */
static DevDisplayStatus display_stop(DevDisplay *self)
{
    uint32_t saved_mask = BSP_Display_EnterCritical(); // 防止扫描在关闭过程中重新选通。
    DevDisplayStatus result = display_check(self); // 当前设备状态。
    if (result == DEV_DISPLAY_OK) {
        s_enabled = false;
        s_current = PORT_DISPLAY_DIGIT_1_INDEX;
        BSP_Display_Blank();
    }
    BSP_Display_ExitCritical(saved_mask);
    return result;
}

/**
 * @brief 提交仅G段点亮的完整横线帧，保持单一扫描源。
 * @param self 数码管设备对象。
 * @return 成功返回OK，失败返回对象或初始化错误。
 */
static DevDisplayStatus display_show_dashes(DevDisplay *self)
{
    uint8_t frame[PORT_DISPLAY_DIGIT_COUNT]; // 保存所有实际数码位的横线字形。
    uint8_t index; // 遍历实际数码位。
    for (index = 0U; index < PORT_DISPLAY_DIGIT_COUNT; ++index)
        frame[index] = PORT_DISPLAY_GLYPH_DASH;
    return display_submit(self, frame);
}

/**
 * @brief 绑定各方法并返回唯一静态对象，不改变显示状态或初始化GPIO。
 * @return 非空设备对象，初始化可用性由init返回值确认。
 */
DevDisplay *GetDisplay(void)
{
    s_display.showDashes = display_show_dashes;
    s_display.init = display_init;
    s_display.scanStep = display_scan_step;
    s_display.clear = display_clear;
    s_display.showDigit = display_show_digit;
#if DISPLAY_CONFIG_DIGIT_COUNT == DISPLAY_CONFIG_FOUR_DIGITS
    s_display.showInteger4 = display_show_integer4;
#endif
    s_display.showFixedPoint = display_show_fixed_point;
    s_display.showError = display_show_error;
    s_display.showAll = display_show_all;
    s_display.stop = display_stop;
    return &s_display;
}
