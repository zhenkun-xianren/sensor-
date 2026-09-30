#include "port_methane.h"
#include "bsp_uart3.h"
#include <stddef.h>

static const uint8_t s_commands[][PORT_METHANE_COMMAND_DATA_LENGTH] = {
    {'R','0'}, {'R','2'}, {'R','4'}, {'R','6'},
    {'R','8'}, {'R','A'}, {'R','C'}, {'F','0'},
    {'F','1'}, {'F','4'}, {'S','1'}, {'S','2'}
};

static MethaneFrameBuffer s_response;
static DevMethane s_methane;

/**
 * @brief 计算字节序列的二进制补码 LRC。
 * @param data 参与 LRC 计算的字节序列。
 * @param length 参与计算的字节数。
 * @return 使序列字节和按 256 取模后为零的 LRC 字节。
 */
uint8_t PortMethane_Lrc(const uint8_t *data, uint8_t length)
{
    uint8_t sum = 0U;
    uint8_t index;
    for (index = 0U; index < length; ++index) {
        sum = (uint8_t)(sum + data[index]);
    }
    return (uint8_t)(0U - sum);
}

/**
 * @brief 将四位数值转换为大写十六进制字符。
 * @param value 待转换的半字节数值。
 * @return 字符 '0' 至 '9' 或 'A' 至 'F'。
 */
static uint8_t hex_digit(uint8_t value)
{
    return value < PORT_METHANE_DECIMAL_RADIX ? (uint8_t)('0' + value) :
           (uint8_t)('A' + value - PORT_METHANE_HEX_ALPHA_OFFSET);
}

/**
 * @brief 将一个 ASCII 十进制或大写十六进制字符转换为半字节数值。
 * @param byte 待解码的字符。
 * @return 有效字符对应的 0 至 15 数值；字符无效时返回 -1。
 */
static int8_t hex_value(uint8_t byte)
{
    if (byte >= '0' && byte <= '9') return (int8_t)(byte - '0');
    if (byte >= 'A' && byte <= 'F') return (int8_t)(byte - 'A' + PORT_METHANE_HEX_ALPHA_OFFSET);
    return -1;
}

/**
 * @brief 构造受支持的传感器命令，并附加 LRC 和行结束符。
 * @param command 要编码的白名单传感器命令。
 * @param frame 接收完整命令帧。
 * @return 编码后的帧长度；命令或输出缓冲区无效时返回 0。
 */
uint8_t PortMethane_BuildCommand(MethaneSafeCommand command,
                                 uint8_t frame[PORT_METHANE_COMMAND_FRAME_LENGTH])
{
    uint8_t lrc;
    if ((unsigned int)command >= sizeof(s_commands) / sizeof(s_commands[0]) || frame == NULL) return 0U;
    frame[PORT_METHANE_COMMAND_FIRST_BYTE_INDEX] = s_commands[command][PORT_METHANE_COMMAND_FIRST_BYTE_INDEX];
    frame[PORT_METHANE_COMMAND_SECOND_BYTE_INDEX] = s_commands[command][PORT_METHANE_COMMAND_SECOND_BYTE_INDEX];
    lrc = PortMethane_Lrc(frame, PORT_METHANE_COMMAND_DATA_LENGTH);
    frame[PORT_METHANE_COMMAND_LRC_SEPARATOR_INDEX] = '\t';
    frame[PORT_METHANE_COMMAND_LRC_HIGH_INDEX] =
        hex_digit((uint8_t)(lrc >> PORT_METHANE_HEX_NIBBLE_BITS));
    frame[PORT_METHANE_COMMAND_LRC_LOW_INDEX] =
        hex_digit((uint8_t)(lrc & PORT_METHANE_HEX_NIBBLE_MASK));
    frame[PORT_METHANE_COMMAND_CR_INDEX] = '\r';
    frame[PORT_METHANE_COMMAND_LF_INDEX] = '\n';
    return PORT_METHANE_COMMAND_FRAME_LENGTH;
}

/**
 * @brief 将带符号定点 ASCII 数值解析为缩放后的整数。
 * @param data 待解析的 ASCII 字段字节。
 * @param length 字段的字节数。
 * @param decimals 小数点后要求的位数。
 * @param result 接收缩放后的整数值。
 * @return 字段有效时返回 1，否则返回 0。
 */
static uint8_t parse_fixed(const uint8_t *data, uint8_t length, uint8_t decimals,
                           int32_t *result)
{
    uint8_t index = 0U;
    uint8_t fraction = 0U;
    uint8_t dot = 0U;
    int32_t value = 0;
    int32_t sign = 1;
    if (length == 0U) return 0U;
    if (data[index] == '+' || data[index] == '-') {
        if (data[index] == '-') sign = -1;
        ++index;
    }
    if (index >= length) return 0U;
    for (; index < length; ++index) {
        if (data[index] == '.') {
            if (dot != 0U || decimals == 0U) return 0U;
            dot = 1U;
        } else if (data[index] >= '0' && data[index] <= '9') {
            if (value > PORT_METHANE_PARSE_MAX_ABS_VALUE) return 0U;
            value = value * PORT_METHANE_DECIMAL_RADIX + (int32_t)(data[index] - '0');
            if (dot != 0U) ++fraction;
        } else {
            return 0U;
        }
    }
    if (dot == 0U || fraction != decimals) return 0U;
    *result = sign * value;
    return 1U;
}

/**
 * @brief 校验并解析一帧传感器 R8 响应。
 * @param frame 接收到的响应字节。
 * @param length 响应的字节数。
 * @param sample 接收解析出的浓度、温度、压力和状态码。
 * @return 有效且成功时返回 DEV_OK；否则返回对应的格式、LRC 或传感器错误。
 */
DevStatus PortMethane_ParseR8(const uint8_t *frame, uint8_t length, MethaneSample *sample)
{
    uint8_t tab = 0U;
    uint8_t commas[PORT_METHANE_RESPONSE_COMMA_COUNT];
    uint8_t comma_count = 0U;
    int8_t hi, lo;
    int32_t concentration, temperature, pressure;
    if (frame == NULL || sample == NULL || length < PORT_METHANE_RESPONSE_MIN_LENGTH) return DEV_FORMAT_ERROR;
    while (tab < length && frame[tab] != '\t') ++tab;
    if (tab < PORT_METHANE_RESPONSE_PREFIX_MIN_LENGTH ||
        (uint8_t)(tab + PORT_METHANE_RESPONSE_TRAILER_LENGTH) != length ||
        frame[tab + PORT_METHANE_RESPONSE_CR_OFFSET] != '\r' ||
        frame[tab + PORT_METHANE_RESPONSE_LF_OFFSET] != '\n') return DEV_FORMAT_ERROR;
    hi = hex_value(frame[tab + PORT_METHANE_RESPONSE_LRC_FIRST_DIGIT_OFFSET]);
    lo = hex_value(frame[tab + PORT_METHANE_RESPONSE_LRC_SECOND_DIGIT_OFFSET]);
    if (hi < 0 || lo < 0) return DEV_FORMAT_ERROR;
    if ((uint8_t)((hi << PORT_METHANE_HEX_NIBBLE_BITS) | lo) != PortMethane_Lrc(frame, tab)) return DEV_LRC_ERROR;
    for (uint8_t index = 0U; index < tab; ++index) {
        if (frame[index] == ',') {
            if (comma_count == PORT_METHANE_RESPONSE_COMMA_COUNT) return DEV_FORMAT_ERROR;
            commas[comma_count++] = index;
        }
    }
    if (comma_count != PORT_METHANE_RESPONSE_COMMA_COUNT || commas[0] == 0U ||
        commas[1] <= commas[0] + PORT_METHANE_FIELD_VALUE_OFFSET ||
        commas[2] <= commas[1] + PORT_METHANE_FIELD_VALUE_OFFSET ||
        tab != commas[2] + PORT_METHANE_STATUS_HEX_DIGIT_COUNT + PORT_METHANE_FIELD_VALUE_OFFSET) {
        return DEV_FORMAT_ERROR;
    }
    if (!parse_fixed(frame, commas[0], PORT_METHANE_CONCENTRATION_DECIMAL_DIGITS, &concentration) ||
        !parse_fixed(&frame[commas[0] + PORT_METHANE_FIELD_VALUE_OFFSET],
                     (uint8_t)(commas[1] - commas[0] - PORT_METHANE_FIELD_VALUE_OFFSET),
                     PORT_METHANE_TEMPERATURE_DECIMAL_DIGITS, &temperature) ||
        !parse_fixed(&frame[commas[1] + PORT_METHANE_FIELD_VALUE_OFFSET],
                     (uint8_t)(commas[2] - commas[1] - PORT_METHANE_FIELD_VALUE_OFFSET),
                     PORT_METHANE_PRESSURE_DECIMAL_DIGITS, &pressure)) return DEV_FORMAT_ERROR;
    hi = hex_value(frame[commas[2] + PORT_METHANE_FIELD_VALUE_OFFSET]);
    lo = hex_value(frame[commas[2] + PORT_METHANE_FIELD_SECOND_VALUE_OFFSET]);
    if (hi < 0 || lo < 0 || concentration < 0 ||
        concentration > PORT_METHANE_MAX_CONCENTRATION_CENTI_VOL ||
        temperature < PORT_METHANE_MIN_TEMPERATURE_DECI_C ||
        temperature > PORT_METHANE_MAX_TEMPERATURE_DECI_C || pressure < 0 ||
        pressure > PORT_METHANE_MAX_PRESSURE_CENTI_MBAR) return DEV_FORMAT_ERROR;
    sample->concentration_centi_vol = (uint16_t)concentration;
    sample->temperature_deci_c = (int16_t)temperature;
    sample->pressure_centi_mbar = (uint32_t)pressure;
    sample->status_code = (uint8_t)((hi << PORT_METHANE_HEX_NIBBLE_BITS) | lo);
    return sample->status_code == 0U ? DEV_OK : DEV_SENSOR_ERROR;
}

/**
 * @brief 清除甲烷响应缓冲区的当前长度和溢出状态。
 * @param buffer 要重置的响应缓冲区。
 */
void PortMethane_BufferReset(MethaneFrameBuffer *buffer)
{
    buffer->length = 0U;
    buffer->overflow = 0U;
}

/**
 * @brief 追加一个响应字节，并在收到行结束符时解析完整帧。
 * @param buffer 正在组装的响应缓冲区。
 * @param byte 新接收的响应字节。
 * @param sample 完整响应有效时接收解析后的样本。
 * @return 完整行到达前返回 DEV_PENDING，否则返回解析状态。
 */
DevStatus PortMethane_BufferPush(MethaneFrameBuffer *buffer, uint8_t byte,
                                 MethaneSample *sample)
{
    DevStatus status;
    if (buffer->length < PORT_METHANE_FRAME_MAX) {
        buffer->data[buffer->length++] = byte;
    } else {
        buffer->overflow = 1U;
    }
    if (byte != '\n') return DEV_PENDING;
    status = buffer->overflow != 0U ? DEV_FORMAT_ERROR :
             PortMethane_ParseR8(buffer->data, buffer->length, sample);
    PortMethane_BufferReset(buffer);
    return status;
}

/**
 * @brief 初始化 USART3 并重置甲烷响应缓冲区。
 * @param self 甲烷传感器设备对象指针。
 */
static void methane_init(DevMethane *self)
{
    (void)self;
    BSP_Uart3_Init();
    PortMethane_BufferReset(&s_response);
}

/**
 * @brief 发送新请求前丢弃未完成响应和 UART 中的待处理字节。
 * @param self 甲烷传感器设备对象指针。
 */
static void methane_reset_response(DevMethane *self)
{
    uint8_t discard;
    (void)self;
    PortMethane_BufferReset(&s_response);
    while (BSP_Uart3_Read(&discard) != 0U) { }
    (void)BSP_Uart3_TakeOverrun();
}

/**
 * @brief 发送一个白名单中的传感器命令。
 * @param self 甲烷传感器设备对象指针。
 * @param command 从受支持的传感器命令集合中选择的命令。
 * @return 发送成功时返回 DEV_OK；命令无效时返回 DEV_FORMAT_ERROR；超时时返回 DEV_IO_ERROR。
 */
static DevStatus methane_send_safe_command(DevMethane *self,
                                           MethaneSafeCommand command)
{
    uint8_t frame[PORT_METHANE_COMMAND_FRAME_LENGTH];
    (void)self;
    if (PortMethane_BuildCommand(command, frame) != PORT_METHANE_COMMAND_FRAME_LENGTH) return DEV_FORMAT_ERROR;
    return BSP_Uart3_Write(frame, PORT_METHANE_COMMAND_FRAME_LENGTH) != 0U ? DEV_OK : DEV_IO_ERROR;
}

/**
 * @brief 清除旧输入并请求新的甲烷传感器样本。
 * @param self 甲烷传感器设备对象指针。
 * @return 请求发送成功时返回 DEV_OK，否则返回发送错误状态。
 */
static DevStatus methane_request_sample(DevMethane *self)
{
    methane_reset_response(self);
    return methane_send_safe_command(self, METHANE_CMD_R8);
}

/**
 * @brief 读取 UART 中可用字节并解析等待中的甲烷样本。
 * @param self 甲烷传感器设备对象指针。
 * @param sample 完整响应到达时接收解析后的样本。
 * @return 等待完整帧时返回 DEV_PENDING，否则返回响应处理状态。
 */
static DevStatus methane_poll_sample(DevMethane *self, MethaneSample *sample)
{
    uint8_t byte;
    DevStatus status;
    (void)self;
    if (sample == NULL) return DEV_FORMAT_ERROR;
    if (BSP_Uart3_TakeOverrun() != 0U) {
        PortMethane_BufferReset(&s_response);
        return DEV_FORMAT_ERROR;
    }
    while (BSP_Uart3_Read(&byte) != 0U) {
        status = PortMethane_BufferPush(&s_response, byte, sample);
        if (status != DEV_PENDING) return status;
    }
    return DEV_PENDING;
}

/**
 * @brief 获取甲烷传感器设备接口并绑定 Port 实现。
 * @return 指向静态甲烷传感器设备接口的指针。
 */
DevMethane *GetMethane(void)
{
    s_methane.init = methane_init;
    s_methane.sendSafeCommand = methane_send_safe_command;
    s_methane.requestSample = methane_request_sample;
    s_methane.pollSample = methane_poll_sample;
    s_methane.resetResponse = methane_reset_response;
    return &s_methane;
}
