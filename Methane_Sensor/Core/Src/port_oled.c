#include "port_oled.h"
#include "dev_oled.h"
#include "bsp_oled.h"
#include <stddef.h>

typedef struct {
    char character; // 当前字形对应的 ASCII 字符。
    uint8_t columns[PORT_OLED_GLYPH_COLUMNS]; // 五列竖向点阵数据，最低位为顶端像素。
} OledGlyph;

static const OledGlyph s_glyphs[] = { // 首行演示、21键名称与未知键十六进制显示所需的5×7字形。
    {' ', {0x00U, 0x00U, 0x00U, 0x00U, 0x00U}},
    {'+', {0x08U, 0x08U, 0x3EU, 0x08U, 0x08U}},
    {'-', {0x08U, 0x08U, 0x08U, 0x08U, 0x08U}},
    {'/', {0x20U, 0x10U, 0x08U, 0x04U, 0x02U}},
    {':', {0x00U, 0x36U, 0x36U, 0x00U, 0x00U}},
    {'?', {0x02U, 0x01U, 0x51U, 0x09U, 0x06U}},
    {'A', {0x7EU, 0x11U, 0x11U, 0x11U, 0x7EU}},
    {'B', {0x7FU, 0x49U, 0x49U, 0x49U, 0x36U}},
    {'C', {0x3EU, 0x41U, 0x41U, 0x41U, 0x22U}},
    {'D', {0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU}},
    {'E', {0x7FU, 0x49U, 0x49U, 0x49U, 0x41U}},
    {'F', {0x7FU, 0x09U, 0x09U, 0x09U, 0x01U}},
    {'K', {0x7FU, 0x08U, 0x14U, 0x22U, 0x41U}},
    {'L', {0x7FU, 0x40U, 0x40U, 0x40U, 0x40U}},
    {'N', {0x7FU, 0x02U, 0x04U, 0x08U, 0x7FU}},
    {'P', {0x7FU, 0x09U, 0x09U, 0x09U, 0x06U}},
    {'Q', {0x3EU, 0x41U, 0x51U, 0x21U, 0x5EU}},
    {'R', {0x7FU, 0x09U, 0x19U, 0x29U, 0x46U}},
    {'S', {0x46U, 0x49U, 0x49U, 0x49U, 0x31U}},
    {'U', {0x3FU, 0x40U, 0x40U, 0x40U, 0x3FU}},
    {'V', {0x1FU, 0x20U, 0x40U, 0x20U, 0x1FU}},
    {'x', {0x44U, 0x28U, 0x10U, 0x28U, 0x44U}},
    {'y', {0x0CU, 0x50U, 0x50U, 0x50U, 0x3CU}},
    {'T', {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U}},
    {'Y', {0x07U, 0x08U, 0x70U, 0x08U, 0x07U}},
    {'X', {0x63U, 0x14U, 0x08U, 0x14U, 0x63U}},
    {'H', {0x7FU, 0x08U, 0x08U, 0x08U, 0x7FU}},
    {'W', {0x7FU, 0x20U, 0x18U, 0x20U, 0x7FU}},
    {'d', {0x38U, 0x44U, 0x44U, 0x48U, 0x7FU}},
    {'e', {0x38U, 0x54U, 0x54U, 0x54U, 0x18U}},
    {'l', {0x00U, 0x41U, 0x7FU, 0x40U, 0x00U}},
    {'o', {0x38U, 0x44U, 0x44U, 0x44U, 0x38U}},
    {'r', {0x7CU, 0x08U, 0x04U, 0x04U, 0x08U}},
    {'0', {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU}},
    {'1', {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U}},
    {'2', {0x42U, 0x61U, 0x51U, 0x49U, 0x46U}},
    {'3', {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U}},
    {'4', {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U}},
    {'5', {0x27U, 0x45U, 0x45U, 0x45U, 0x39U}},
    {'6', {0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U}},
    {'7', {0x01U, 0x71U, 0x09U, 0x05U, 0x03U}},
    {'8', {0x36U, 0x49U, 0x49U, 0x49U, 0x36U}},
    {'9', {0x06U, 0x49U, 0x49U, 0x29U, 0x1EU}}
};
static const char s_prefix[] = "HelloWorld "; // 首行固定文字与数字之间的空格。
static const uint8_t s_blank_page[PORT_OLED_COLUMNS] = {0U}; // 初始化时用于清除各显示页的空白数据。
static DevOled s_oled; // 供 APP 调用的静态 OLED 设备接口对象。
static uint8_t s_initialized; // OLED 已完成初始化并可写入显示数据的状态。

/**
 * @brief 设置 OLED 页地址和第零列地址。
 * @param page 目标显示页编号，范围为 0 至 7。
 * @return 命令发送成功返回 1，通信失败返回 0。
 */
static uint8_t oled_set_page(uint8_t page)
{
    uint8_t commands[3] = { // 页地址、列地址低位和高位命令。
        (uint8_t)(PORT_OLED_PAGE_BASE | page),
        PORT_OLED_COLUMN_LOW_ZERO,
        PORT_OLED_COLUMN_HIGH_ZERO
    };

    return BSP_Oled_Write(commands, (uint16_t)sizeof(commands), 0U);
}

/**
 * @brief 查找显示字符的五列点阵。
 * @param character 需要绘制的 ASCII 字符。
 * @return 对应字形的只读指针；未知字符返回问号字形。
 */
static const OledGlyph *oled_find_glyph(char character)
{
    uint32_t index; // 遍历演示专用字形表的索引。

    for (index = 0U; index < sizeof(s_glyphs) / sizeof(s_glyphs[0]); ++index) {
        if (s_glyphs[index].character == character) {
            return &s_glyphs[index];
        }
    }
    return &s_glyphs[5];
}

/**
 * @brief 在单行缓存指定列写入一个5×7字形和一列间隔。
 * @param frame 单行128字节显示缓存。
 * @param column 该字符起始列，必须留有 6 列空间。
 * @param character 需要绘制的 ASCII 字符。
 * @return 写入后下一字符的起始列。
 */
static uint8_t oled_append_glyph(uint8_t frame[PORT_OLED_COLUMNS],
                                 uint8_t column, char character)
{
    const OledGlyph *glyph = oled_find_glyph(character); // 本字符对应的五列字形。
    uint8_t glyph_column; // 正在复制的字形列索引。

    for (glyph_column = 0U; glyph_column < PORT_OLED_GLYPH_COLUMNS;
         ++glyph_column) {
        frame[column + glyph_column] = glyph->columns[glyph_column];
    }
    frame[column + PORT_OLED_GLYPH_COLUMNS] = 0U;
    return (uint8_t)(column + PORT_OLED_GLYPH_ADVANCE);
}

/**
 * @brief 初始化 OLED 模块并按照模块手册清除全部显示页。
 * @param self 接收初始化请求的 OLED 设备对象。
 * @return 初始化和清屏成功返回 1，失败返回 0。
 */
static uint8_t oled_init(DevOled *self)
{
    static const uint8_t init_commands[] = { // 模块手册 PDF 第 16 页给出的初始化命令及参数。
        PORT_OLED_CMD_UNLOCK, PORT_OLED_CMD_UNLOCK_VALUE,
        PORT_OLED_CMD_DISPLAY_OFF,
        PORT_OLED_CMD_CLOCK, PORT_OLED_CLOCK_VALUE,
        PORT_OLED_CMD_MULTIPLEX, PORT_OLED_MULTIPLEX_64,
        PORT_OLED_CMD_OFFSET, PORT_OLED_OFFSET_ZERO,
        PORT_OLED_START_LINE_ZERO,
        PORT_OLED_SEGMENT_REMAP, PORT_OLED_COM_REMAP,
        PORT_OLED_CMD_COM_PINS, PORT_OLED_COM_PINS_VALUE,
        PORT_OLED_CMD_CONTRAST, PORT_OLED_CONTRAST_VALUE,
        PORT_OLED_CMD_PRECHARGE, PORT_OLED_PRECHARGE_VALUE,
        PORT_OLED_CMD_VCOMH, PORT_OLED_VCOMH_VALUE,
        PORT_OLED_DISPLAY_RAM, PORT_OLED_DISPLAY_NORMAL
    };
    uint8_t page; // 初始化清屏时的显示页编号。
    uint8_t display_on = PORT_OLED_CMD_DISPLAY_ON; // 清屏结束后开启面板的命令。

    s_initialized = 0U;
    if (self == NULL || BSP_Oled_Init() == 0U ||
        BSP_Oled_Write(init_commands, (uint16_t)sizeof(init_commands), 0U) == 0U) {
        return 0U;
    }
    for (page = 0U; page < PORT_OLED_PAGES; ++page) {
        if (oled_set_page(page) == 0U ||
            BSP_Oled_Write(s_blank_page, PORT_OLED_COLUMNS, 1U) == 0U) {
            return 0U;
        }
    }
    if (BSP_Oled_Write(&display_on, 1U, 0U) == 0U) {
        return 0U;
    }
    s_initialized = 1U;
    return 1U;
}

/**
 * @brief 将 HelloWorld 和完整 uint32 十进制数字绘制到首行。
 * @param self 接收显示请求的 OLED 设备对象。
 * @param value 需要显示的无符号计数值。
 * @return 显示发送成功返回 1，设备未就绪或通信失败返回 0。
 */
static uint8_t oled_show_counter(DevOled *self, uint32_t value)
{
    uint8_t frame[PORT_OLED_COLUMNS] = {0U}; // 首行整页缓存，清掉旧数字留下的笔画。
    char digits[PORT_OLED_DECIMAL_DIGITS_MAX]; // 十进制数字的逆序字符缓存。
    uint8_t digit_count = 0U; // 本次十进制数字的位数。
    uint8_t column = 0U; // 下一字符在首行中的起始列。
    uint8_t index; // 遍历固定文本或逆序数字的索引。

    if (self == NULL || s_initialized == 0U) {
        return 0U;
    }
    do {
        digits[digit_count] = (char)('0' + (value % 10U));
        ++digit_count;
        value /= 10U;
    } while (value != 0U && digit_count < PORT_OLED_DECIMAL_DIGITS_MAX);

    for (index = 0U; s_prefix[index] != '\0'; ++index) {
        column = oled_append_glyph(frame, column, s_prefix[index]);
    }
    while (digit_count > 0U) {
        --digit_count;
        column = oled_append_glyph(frame, column, digits[digit_count]);
    }
    return (oled_set_page(0U) != 0U &&
            BSP_Oled_Write(frame, PORT_OLED_COLUMNS, 1U) != 0U) ? 1U : 0U;
}

/**
 * @brief 绘制整行文本，整页发送使短文本不会保留旧字符。
 * @param self 接收请求的 OLED 设备对象。
 * @param line 目标页号，范围0至7。
 * @param text 零结尾文本，最多21个可打印ASCII字符。
 * @return 写入成功返回1，参数或通信失败返回0。
 */
static uint8_t oled_write_line(DevOled *self, uint8_t line, const char *text)
{
    uint8_t frame[PORT_OLED_COLUMNS] = {0U}; // 本行完整页缓存，未绘制区域保持空白。
    uint8_t length = 0U; // 已检查的文本字符数。
    uint8_t column = 0U; // 下一字形的起始列。
    uint8_t index; // 当前绘制字符的索引。
    if (self == NULL || s_initialized == 0U || text == NULL || line >= DEV_OLED_LINE_COUNT) {
        return 0U;
    }
    while (text[length] != '\0') {
        if (length >= DEV_OLED_TEXT_MAX || (uint8_t)text[length] < 32U ||
            (uint8_t)text[length] > 126U) return 0U;
        ++length;
    }
    for (index = 0U; index < length; ++index) {
        column = oled_append_glyph(frame, column, text[index]);
    }
    return oled_set_page(line) != 0U &&
           BSP_Oled_Write(frame, PORT_OLED_COLUMNS, 1U) != 0U ? 1U : 0U;
}

/**
 * @brief 获取按 Device 契约绑定的 OLED 静态设备对象。
 * @return 指向 OLED 设备对象的指针。
 */
DevOled *GetOled(void)
{
    s_oled.init = oled_init;
    s_oled.showCounter = oled_show_counter;
    s_oled.writeLine = oled_write_line;
    return &s_oled;
}
