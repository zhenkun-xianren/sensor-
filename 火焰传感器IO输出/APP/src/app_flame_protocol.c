#include <stddef.h>
#include "app_flame_protocol.h"

/** @brief 用现有CRC参数校验指定字节数。
 * @param data 待校验字节。
 * @param length 不含尾部CRC的长度。
 * @return 16位CRC累计值。 */
static uint16_t app_flame_crc(const uint8_t *data, uint16_t length)
{
    uint16_t crc = APP_FLAME_CRC_INITIAL; // 初始累计校验值。
    uint16_t index; // 当前字节位置。
    for (index = 0; index < length; ++index) {
        uint8_t bit; // 当前字节内的位位置。
        crc ^= data[index];
        for (bit = 0; bit < APP_FLAME_BYTE_BITS; ++bit) {
            crc = (crc & APP_FLAME_CRC_LOW_BIT) ?
                (uint16_t)((crc >> APP_FLAME_CRC_SHIFT) ^ APP_FLAME_CRC_POLYNOMIAL) :
                (uint16_t)(crc >> APP_FLAME_CRC_SHIFT);
        }
    }
    return crc;
}

/** @brief 写入低字节在前的整数。
 * @param output 目标字段地址。
 * @param value 待编码值。
 * @param bytes 字段字节数。 */
static void app_flame_put(uint8_t *output, uint32_t value, uint8_t bytes)
{
    uint8_t index; // 字段内字节位置。
    for (index = 0; index < bytes; ++index) {
        output[index] = (uint8_t)(value >> (index * APP_FLAME_BYTE_BITS));
    }
}

/** @brief 读取低字节在前的整数。
 * @param input 字段起始地址。
 * @param bytes 字段字节数。
 * @return 解码后的无符号整数。 */
static uint32_t app_flame_get(const uint8_t *input, uint8_t bytes)
{
    uint32_t value = 0; // 汇总字段内的位。
    uint8_t index; // 当前字段的字节位置。
    for (index = 0; index < bytes; ++index) {
        value |= (uint32_t)input[index] << (index * APP_FLAME_BYTE_BITS);
    }
    return value;
}

/** @brief 校验分钟报告的类型、时间范围、通道顺序和诊断字段。
 * @param report 非空分钟快照。
 * @return 所有字段满足分钟帧契约时返回true。 */
static bool app_flame_minute_report_valid(const AppFlameMinuteReport *report)
{
    uint8_t index; // 需要检查的固定顺序通道索引。
    if (((report->type != APP_FLAME_TYPE_MINUTE_PERIODIC) &&
         (report->type != APP_FLAME_TYPE_MINUTE_CHANGE)) ||
        (report->elapsed_ms >= APP_FLAME_MINUTE_PERIOD_MS)) {
        return false;
    }
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        const AppFlameMinuteChannelReport *channel = &report->channels[index]; // 当前通道的待校验字段。
        if ((channel->channel_id != (uint8_t)(index + APP_FLAME_CHANNEL_ID_FIRST)) ||
            (channel->adc_raw > APP_FLAME_ADC_MAX) || (channel->config_raw > APP_FLAME_CONFIG_MAX) ||
            ((channel->flags & ~APP_FLAME_MINUTE_KNOWN_FLAGS) != APP_FLAME_EMPTY_FLAGS)) {
            return false;
        }
    }
    return true;
}

/** @brief 逐字段编码80字节分钟帧，不发送C结构体的填充字节。
 * @param report 非空三路分钟快照。
 * @param output 非空帧输出缓冲区。
 * @param capacity 缓冲区容量，单位字节。
 * @return 参数、字段有效且编码完成返回true。 */
bool AppFlameMinuteFrame_Encode(const AppFlameMinuteReport *report, uint8_t *output, uint16_t capacity)
{
    uint8_t index; // 当前通道编码索引。
    uint16_t crc; // 前78字节的CRC校验值。
    if ((report == NULL) || (output == NULL) || (capacity < APP_FLAME_MINUTE_FRAME_BYTES) ||
        !app_flame_minute_report_valid(report)) {
        return false;
    }
    output[APP_FLAME_HEADER_FIRST_OFFSET] = APP_FLAME_HEADER_FIRST;
    output[APP_FLAME_HEADER_SECOND_OFFSET] = APP_FLAME_HEADER_SECOND;
    output[APP_FLAME_VERSION_OFFSET] = APP_FLAME_PROTOCOL_VERSION;
    output[APP_FLAME_TYPE_OFFSET] = report->type;
    output[APP_FLAME_ADDRESS_OFFSET] = report->address;
    app_flame_put(&output[APP_FLAME_SEQUENCE_OFFSET], report->sequence, APP_FLAME_WORD_BYTES);
    app_flame_put(&output[APP_FLAME_LENGTH_OFFSET], APP_FLAME_MINUTE_PAYLOAD_BYTES, APP_FLAME_WORD_BYTES);
    app_flame_put(&output[APP_FLAME_UPTIME_OFFSET], report->uptime_ms, APP_FLAME_DWORD_BYTES);
    app_flame_put(&output[APP_FLAME_MINUTE_INDEX_OFFSET], report->minute_index, APP_FLAME_DWORD_BYTES);
    app_flame_put(&output[APP_FLAME_MINUTE_ELAPSED_OFFSET], report->elapsed_ms, APP_FLAME_DWORD_BYTES);
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        const AppFlameMinuteChannelReport *channel = &report->channels[index]; // 本路逻辑字段来源。
        uint8_t *field = &output[APP_FLAME_MINUTE_CHANNELS_OFFSET + index * APP_FLAME_MINUTE_CHANNEL_BYTES]; // 本路19字节块。
        field[APP_FLAME_CHANNEL_ID_OFFSET] = channel->channel_id;
        app_flame_put(&field[APP_FLAME_MINUTE_FLAGS_OFFSET], channel->flags, APP_FLAME_WORD_BYTES);
        app_flame_put(&field[APP_FLAME_MINUTE_RESULT_OFFSET], (uint16_t)(int16_t)channel->result, APP_FLAME_WORD_BYTES);
        app_flame_put(&field[APP_FLAME_MINUTE_ADC_OFFSET], channel->adc_raw, APP_FLAME_WORD_BYTES);
        app_flame_put(&field[APP_FLAME_MINUTE_CONFIG_OFFSET], channel->config_raw, APP_FLAME_DWORD_BYTES);
        app_flame_put(&field[APP_FLAME_MINUTE_AGE_OFFSET], channel->sample_age_ms, APP_FLAME_DWORD_BYTES);
        app_flame_put(&field[APP_FLAME_MINUTE_COUNT_OFFSET], channel->event_count, APP_FLAME_DWORD_BYTES);
    }
    crc = app_flame_crc(output, APP_FLAME_MINUTE_CRC_OFFSET);
    app_flame_put(&output[APP_FLAME_MINUTE_CRC_OFFSET], crc, APP_FLAME_WORD_BYTES);
    return true;
}

/** @brief 校验分钟帧后暂存解码字段，全部有效才提交输出。
 * @param input 非空完整字节序列。
 * @param length 实际字节数，必须为80。
 * @param report 非空报告输出对象。
 * @return 编码版本、长度、CRC及字段均有效返回true，失败不修改输出。 */
bool AppFlameMinuteFrame_Decode(const uint8_t *input, uint16_t length, AppFlameMinuteReport *report)
{
    AppFlameMinuteReport temporary = {0}; // 暂存完整解码结果，保证失败时不污染调用方输出。
    uint8_t index; // 当前通道解码索引。
    if ((input == NULL) || (report == NULL) || (length != APP_FLAME_MINUTE_FRAME_BYTES) ||
        (input[APP_FLAME_HEADER_FIRST_OFFSET] != APP_FLAME_HEADER_FIRST) ||
        (input[APP_FLAME_HEADER_SECOND_OFFSET] != APP_FLAME_HEADER_SECOND) ||
        (input[APP_FLAME_VERSION_OFFSET] != APP_FLAME_PROTOCOL_VERSION) ||
        (app_flame_get(&input[APP_FLAME_LENGTH_OFFSET], APP_FLAME_WORD_BYTES) != APP_FLAME_MINUTE_PAYLOAD_BYTES) ||
        (app_flame_get(&input[APP_FLAME_MINUTE_CRC_OFFSET], APP_FLAME_WORD_BYTES) !=
         app_flame_crc(input, APP_FLAME_MINUTE_CRC_OFFSET))) {
        return false;
    }
    temporary.type = input[APP_FLAME_TYPE_OFFSET];
    temporary.address = input[APP_FLAME_ADDRESS_OFFSET];
    temporary.sequence = (uint16_t)app_flame_get(&input[APP_FLAME_SEQUENCE_OFFSET], APP_FLAME_WORD_BYTES);
    temporary.uptime_ms = app_flame_get(&input[APP_FLAME_UPTIME_OFFSET], APP_FLAME_DWORD_BYTES);
    temporary.minute_index = app_flame_get(&input[APP_FLAME_MINUTE_INDEX_OFFSET], APP_FLAME_DWORD_BYTES);
    temporary.elapsed_ms = app_flame_get(&input[APP_FLAME_MINUTE_ELAPSED_OFFSET], APP_FLAME_DWORD_BYTES);
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        const uint8_t *field = &input[APP_FLAME_MINUTE_CHANNELS_OFFSET + index * APP_FLAME_MINUTE_CHANNEL_BYTES]; // 本路线上字段。
        AppFlameMinuteChannelReport *channel = &temporary.channels[index]; // 本路暂存逻辑字段。
        uint16_t result_bits = (uint16_t)app_flame_get(&field[APP_FLAME_MINUTE_RESULT_OFFSET], APP_FLAME_WORD_BYTES); // 结果码补码位。
        channel->channel_id = field[APP_FLAME_CHANNEL_ID_OFFSET];
        channel->flags = (uint16_t)app_flame_get(&field[APP_FLAME_MINUTE_FLAGS_OFFSET], APP_FLAME_WORD_BYTES);
        channel->result = (DevFlameResult)((result_bits & APP_FLAME_SIGN_BIT) ?
            (int32_t)result_bits - APP_FLAME_WORD_MODULUS : result_bits);
        channel->adc_raw = (uint16_t)app_flame_get(&field[APP_FLAME_MINUTE_ADC_OFFSET], APP_FLAME_WORD_BYTES);
        channel->config_raw = app_flame_get(&field[APP_FLAME_MINUTE_CONFIG_OFFSET], APP_FLAME_DWORD_BYTES);
        channel->sample_age_ms = app_flame_get(&field[APP_FLAME_MINUTE_AGE_OFFSET], APP_FLAME_DWORD_BYTES);
        channel->event_count = app_flame_get(&field[APP_FLAME_MINUTE_COUNT_OFFSET], APP_FLAME_DWORD_BYTES);
    }
    if (!app_flame_minute_report_valid(&temporary)) {
        return false;
    }
    *report = temporary;
    return true;
}

/** @brief 校验允许的三态结果。
 * @param decision 待检查的枚举值。
 * @return 为已定义三态之一返回true。 */
static bool app_flame_decision_valid(AppFlameDecision decision)
{
    return (decision == APP_FLAME_NOT_DETECTED) ||
           (decision == APP_FLAME_TRIGGERED) || (decision == APP_FLAME_UNKNOWN);
}

/** @brief 校验快照关键字段，拒绝未知版本语义。
 * @param report 待编码或解码的报告。
 * @return 类型、通道、结果及原始码范围符合V1时返回true。 */
static bool app_flame_report_valid(const AppFlameReport *report)
{
    uint8_t index; // 固定顺序的通道索引。
    if (!app_flame_decision_valid(report->decision) ||
        ((report->type != APP_FLAME_TYPE_PERIODIC) && (report->type != APP_FLAME_TYPE_CHANGE))) {
        return false;
    }
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        const AppFlameChannelReport *channel = &report->channels[index]; // 当前通道快照。
        if ((channel->channel_id != (uint8_t)(index + APP_FLAME_CHANNEL_ID_FIRST)) ||
            !app_flame_decision_valid(channel->decision) ||
            (channel->adc_raw > APP_FLAME_ADC_MAX) ||
            (channel->config_raw > APP_FLAME_CONFIG_MAX) ||
            ((channel->flags & ~APP_FLAME_KNOWN_FLAGS) != APP_FLAME_EMPTY_FLAGS)) {
            return false;
        }
    }
    return true;
}

/** @brief 逐字段编码，避免编译器结构体对齐影响线路布局。
 * @param report 三路状态快照。
 * @param output 非空输出缓冲区。
 * @param capacity 输出缓冲区字节数。
 * @return 编码完成返回true，无效参数返回false。 */
bool AppFlameFrame_Encode(const AppFlameReport *report, uint8_t *output, uint16_t capacity)
{
    uint8_t index; // 当前通道索引。
    uint16_t crc; // 整帧末尾CRC。
    if ((report == NULL) || (output == NULL) || (capacity < APP_FLAME_FRAME_BYTES) ||
        !app_flame_report_valid(report)) {
        return false;
    }
    output[APP_FLAME_HEADER_FIRST_OFFSET] = APP_FLAME_HEADER_FIRST;
    output[APP_FLAME_HEADER_SECOND_OFFSET] = APP_FLAME_HEADER_SECOND;
    output[APP_FLAME_VERSION_OFFSET] = APP_FLAME_PROTOCOL_VERSION;
    output[APP_FLAME_TYPE_OFFSET] = report->type;
    output[APP_FLAME_ADDRESS_OFFSET] = report->address;
    app_flame_put(&output[APP_FLAME_SEQUENCE_OFFSET], report->sequence, APP_FLAME_WORD_BYTES);
    app_flame_put(&output[APP_FLAME_LENGTH_OFFSET], APP_FLAME_PAYLOAD_BYTES, APP_FLAME_WORD_BYTES);
    app_flame_put(&output[APP_FLAME_UPTIME_OFFSET], report->uptime_ms, APP_FLAME_DWORD_BYTES);
    output[APP_FLAME_DECISION_OFFSET] = (uint8_t)report->decision;
    output[APP_FLAME_METHOD_OFFSET] = APP_FLAME_METHOD_INTERNAL;
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        const AppFlameChannelReport *channel = &report->channels[index]; // 当前通道字段来源。
        uint8_t *field = &output[APP_FLAME_CHANNELS_OFFSET + index * APP_FLAME_CHANNEL_BYTES]; // 当前通道线路位置。
        field[APP_FLAME_CHANNEL_ID_OFFSET] = channel->channel_id;
        field[APP_FLAME_CHANNEL_DECISION_OFFSET] = (uint8_t)channel->decision;
        app_flame_put(&field[APP_FLAME_FLAGS_OFFSET], channel->flags, APP_FLAME_WORD_BYTES);
        app_flame_put(&field[APP_FLAME_RESULT_OFFSET], (uint16_t)(int16_t)channel->result, APP_FLAME_WORD_BYTES);
        app_flame_put(&field[APP_FLAME_ADC_OFFSET], channel->adc_raw, APP_FLAME_WORD_BYTES);
        app_flame_put(&field[APP_FLAME_CONFIG_OFFSET], channel->config_raw, APP_FLAME_DWORD_BYTES);
        app_flame_put(&field[APP_FLAME_AGE_OFFSET], channel->sample_age_ms, APP_FLAME_DWORD_BYTES);
    }
    crc = app_flame_crc(output, APP_FLAME_CRC_OFFSET);
    app_flame_put(&output[APP_FLAME_CRC_OFFSET], crc, APP_FLAME_WORD_BYTES);
    return true;
}

/** @brief 校验三路初始化快照并逐字段编码，不使用结构体内存作为线上布局。
 * @param report 初始化快照的非空源对象。
 * @param output 非空帧缓冲区。
 * @param capacity 帧缓冲区容量，不小于INIT_FRAME_BYTES。
 * @return 编码成功返回true，参数、状态或次数无效返回false。 */
bool AppFlameInitFrame_Encode(const AppFlameInitReport *report, uint8_t *output, uint16_t capacity)
{
    uint8_t index; // 当前初始化通道索引。
    uint16_t crc; // 初始化状态帧尾部校验值。
    if ((report == NULL) || (output == NULL) || (capacity < APP_FLAME_INIT_FRAME_BYTES)) {
        return false;
    }
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        const AppFlameInitChannelReport *channel = &report->channels[index]; // 待校验的本路初始化报告。
        if ((channel->channel_id != (uint8_t)(index + APP_FLAME_CHANNEL_ID_FIRST)) ||
            ((channel->status != APP_FLAME_INIT_NOT_CONFIGURED) &&
             (channel->status != APP_FLAME_INIT_SUCCESS) &&
             (channel->status != APP_FLAME_INIT_RETRYING) &&
             (channel->status != APP_FLAME_INIT_FAILED)) ||
            (channel->retries_done > report->retry_limit)) {
            return false;
        }
        if (((channel->status == APP_FLAME_INIT_SUCCESS) && (channel->result != DEV_FLAME_OK)) ||
            ((channel->status == APP_FLAME_INIT_RETRYING) &&
             ((channel->result == DEV_FLAME_OK) || (channel->retries_done >= report->retry_limit))) ||
            ((channel->status == APP_FLAME_INIT_NOT_CONFIGURED) &&
             ((channel->result != DEV_FLAME_NOT_READY) || (channel->retries_done != 0)))) {
            return false;
        }
    }
    output[APP_FLAME_HEADER_FIRST_OFFSET] = APP_FLAME_HEADER_FIRST;
    output[APP_FLAME_HEADER_SECOND_OFFSET] = APP_FLAME_HEADER_SECOND;
    output[APP_FLAME_VERSION_OFFSET] = APP_FLAME_PROTOCOL_VERSION;
    output[APP_FLAME_TYPE_OFFSET] = APP_FLAME_TYPE_INITIALIZATION;
    output[APP_FLAME_ADDRESS_OFFSET] = report->address;
    app_flame_put(&output[APP_FLAME_SEQUENCE_OFFSET], report->sequence, APP_FLAME_WORD_BYTES);
    app_flame_put(&output[APP_FLAME_LENGTH_OFFSET], APP_FLAME_INIT_PAYLOAD_BYTES, APP_FLAME_WORD_BYTES);
    app_flame_put(&output[APP_FLAME_UPTIME_OFFSET], report->uptime_ms, APP_FLAME_DWORD_BYTES);
    app_flame_put(&output[APP_FLAME_INIT_RETRY_LIMIT_OFFSET], report->retry_limit, APP_FLAME_DWORD_BYTES);
    app_flame_put(&output[APP_FLAME_INIT_RETRY_INTERVAL_OFFSET], report->retry_interval_ms, APP_FLAME_DWORD_BYTES);
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        const AppFlameInitChannelReport *channel = &report->channels[index]; // 当前通道初始化字段来源。
        uint8_t *field = &output[APP_FLAME_INIT_CHANNELS_OFFSET + index * APP_FLAME_INIT_CHANNEL_BYTES]; // 当前通道线上位置。
        field[APP_FLAME_CHANNEL_ID_OFFSET] = channel->channel_id;
        field[APP_FLAME_INIT_STATUS_OFFSET] = (uint8_t)channel->status;
        app_flame_put(&field[APP_FLAME_INIT_RESULT_OFFSET], (uint16_t)(int16_t)channel->result, APP_FLAME_WORD_BYTES);
        app_flame_put(&field[APP_FLAME_INIT_RETRIES_OFFSET], channel->retries_done, APP_FLAME_DWORD_BYTES);
    }
    crc = app_flame_crc(output, APP_FLAME_INIT_CRC_OFFSET);
    app_flame_put(&output[APP_FLAME_INIT_CRC_OFFSET], crc, APP_FLAME_WORD_BYTES);
    return true;
}

/** @brief 校验清除结果字段并编码固定应答，重传时可直接复用原编码字节。
 * @param report 执行清除时保存的固定结果快照。
 * @param output 接收26字节帧的非空缓冲区。
 * @param capacity 输出缓冲区字节数。
 * @return 结果、位图一致且编码成功返回true，否则返回false。 */
bool AppFlameClearFrame_Encode(const AppFlameClearReport *report, uint8_t *output, uint16_t capacity)
{
    uint8_t index; // 本次结果的通道索引。
    uint16_t crc; // 固定应答的尾部校验值。
    uint8_t affected_mask; // 未初始化或清除失败通道的并集。
    if ((report == NULL) || (output == NULL) || (capacity < APP_FLAME_CLEAR_RESULT_FRAME_BYTES)) {
        return false;
    }
    affected_mask = report->not_initialized_mask | report->failed_mask;
    if (((affected_mask & ~APP_FLAME_CLEAR_CHANNEL_MASK) != APP_FLAME_CLEAR_NO_CHANNELS) ||
        ((report->not_initialized_mask & report->failed_mask) != APP_FLAME_CLEAR_NO_CHANNELS) ||
        (report->success != (affected_mask == APP_FLAME_CLEAR_NO_CHANNELS))) {
        return false;
    }
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        uint8_t channel_bit = (uint8_t)(APP_FLAME_CLEAR_CHANNEL_BIT << index); // 本路位图掩码。
        DevFlameResult result = report->results[index]; // 本次清除保存的结果，不能使用实时采样状态。
        if (((report->not_initialized_mask & channel_bit) != APP_FLAME_CLEAR_NO_CHANNELS) ?
            (result != DEV_FLAME_NOT_READY) :
            (((report->failed_mask & channel_bit) != APP_FLAME_CLEAR_NO_CHANNELS) ?
             (result == DEV_FLAME_OK) : (result != DEV_FLAME_OK))) {
            return false;
        }
    }
    output[APP_FLAME_HEADER_FIRST_OFFSET] = APP_FLAME_HEADER_FIRST;
    output[APP_FLAME_HEADER_SECOND_OFFSET] = APP_FLAME_HEADER_SECOND;
    output[APP_FLAME_VERSION_OFFSET] = APP_FLAME_PROTOCOL_VERSION;
    output[APP_FLAME_TYPE_OFFSET] = APP_FLAME_TYPE_CLEAR_RESULT;
    output[APP_FLAME_ADDRESS_OFFSET] = report->address;
    app_flame_put(&output[APP_FLAME_SEQUENCE_OFFSET], report->sequence, APP_FLAME_WORD_BYTES);
    app_flame_put(&output[APP_FLAME_LENGTH_OFFSET], APP_FLAME_CLEAR_RESULT_PAYLOAD_BYTES, APP_FLAME_WORD_BYTES);
    app_flame_put(&output[APP_FLAME_UPTIME_OFFSET], report->uptime_ms, APP_FLAME_DWORD_BYTES);
    app_flame_put(&output[APP_FLAME_CLEAR_REQUEST_SEQUENCE_OFFSET], report->request_sequence, APP_FLAME_WORD_BYTES);
    output[APP_FLAME_CLEAR_SUCCESS_OFFSET] = report->success ? APP_FLAME_CLEAR_SUCCESS : APP_FLAME_CLEAR_FAILURE;
    output[APP_FLAME_CLEAR_NOT_INITIALIZED_OFFSET] = report->not_initialized_mask;
    output[APP_FLAME_CLEAR_FAILED_OFFSET] = report->failed_mask;
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        app_flame_put(&output[APP_FLAME_CLEAR_RESULTS_OFFSET + index * APP_FLAME_WORD_BYTES],
                      (uint16_t)(int16_t)report->results[index], APP_FLAME_WORD_BYTES);
    }
    crc = app_flame_crc(output, APP_FLAME_CLEAR_RESULT_CRC_OFFSET);
    app_flame_put(&output[APP_FLAME_CLEAR_RESULT_CRC_OFFSET], crc, APP_FLAME_WORD_BYTES);
    return true;
}

/** @brief 完整校验后再提交解码快照。
 * @param input 收到的完整帧。
 * @param length 输入字节数。
 * @param report 解码结果的非空输出对象。
 * @return V1帧有效返回true，失败保持输出不变。 */
bool AppFlameFrame_Decode(const uint8_t *input, uint16_t length, AppFlameReport *report)
{
    AppFlameReport decoded = {0}; // 暂存解码内容，失败不污染调用者数据。
    uint8_t index; // 当前解码通道。
    if ((input == NULL) || (report == NULL) || (length != APP_FLAME_FRAME_BYTES)) {
        return false;
    }
    if ((input[APP_FLAME_HEADER_FIRST_OFFSET] != APP_FLAME_HEADER_FIRST) ||
        (input[APP_FLAME_HEADER_SECOND_OFFSET] != APP_FLAME_HEADER_SECOND) ||
        (input[APP_FLAME_VERSION_OFFSET] != APP_FLAME_PROTOCOL_VERSION) ||
        (input[APP_FLAME_METHOD_OFFSET] != APP_FLAME_METHOD_INTERNAL) ||
        (app_flame_get(&input[APP_FLAME_LENGTH_OFFSET], APP_FLAME_WORD_BYTES) != APP_FLAME_PAYLOAD_BYTES) ||
        (app_flame_crc(input, APP_FLAME_CRC_OFFSET) !=
         app_flame_get(&input[APP_FLAME_CRC_OFFSET], APP_FLAME_WORD_BYTES))) {
        return false;
    }
    decoded.type = input[APP_FLAME_TYPE_OFFSET];
    decoded.address = input[APP_FLAME_ADDRESS_OFFSET];
    decoded.sequence = (uint16_t)app_flame_get(&input[APP_FLAME_SEQUENCE_OFFSET], APP_FLAME_WORD_BYTES);
    decoded.uptime_ms = app_flame_get(&input[APP_FLAME_UPTIME_OFFSET], APP_FLAME_DWORD_BYTES);
    decoded.decision = (AppFlameDecision)input[APP_FLAME_DECISION_OFFSET];
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        const uint8_t *field = &input[APP_FLAME_CHANNELS_OFFSET + index * APP_FLAME_CHANNEL_BYTES]; // 当前通道输入。
        AppFlameChannelReport *channel = &decoded.channels[index]; // 当前通道输出。
        uint16_t encoded_result = (uint16_t)app_flame_get(&field[APP_FLAME_RESULT_OFFSET], APP_FLAME_WORD_BYTES); // 补码位型。
        int32_t signed_result = (encoded_result & APP_FLAME_SIGN_BIT) ?
            (int32_t)encoded_result - APP_FLAME_WORD_MODULUS : encoded_result; // 显式补码转有符号。
        channel->channel_id = field[APP_FLAME_CHANNEL_ID_OFFSET];
        channel->decision = (AppFlameDecision)field[APP_FLAME_CHANNEL_DECISION_OFFSET];
        channel->flags = (uint16_t)app_flame_get(&field[APP_FLAME_FLAGS_OFFSET], APP_FLAME_WORD_BYTES);
        channel->result = (DevFlameResult)signed_result;
        channel->adc_raw = (uint16_t)app_flame_get(&field[APP_FLAME_ADC_OFFSET], APP_FLAME_WORD_BYTES);
        channel->config_raw = app_flame_get(&field[APP_FLAME_CONFIG_OFFSET], APP_FLAME_DWORD_BYTES);
        channel->sample_age_ms = app_flame_get(&field[APP_FLAME_AGE_OFFSET], APP_FLAME_DWORD_BYTES);
    }
    if (!app_flame_report_valid(&decoded)) {
        return false;
    }
    *report = decoded;
    return true;
}

/** @brief 严格校验11字节清除命令，不把普通上报帧解释成操作命令。
 * @param input 串口收到的完整原始帧。
 * @param length 帧字节数。
 * @param address 当前设备地址。
 * @return 帧头、版本、类型、地址、零载荷和CRC均正确返回true。 */
bool AppFlameCommand_IsClear(const uint8_t *input, uint16_t length, uint8_t address)
{
    if ((input == NULL) || (length != APP_FLAME_CLEAR_FRAME_BYTES)) {
        return false;
    }
    return (input[APP_FLAME_HEADER_FIRST_OFFSET] == APP_FLAME_HEADER_FIRST) &&
           (input[APP_FLAME_HEADER_SECOND_OFFSET] == APP_FLAME_HEADER_SECOND) &&
           (input[APP_FLAME_VERSION_OFFSET] == APP_FLAME_PROTOCOL_VERSION) &&
           (input[APP_FLAME_TYPE_OFFSET] == APP_FLAME_TYPE_CLEAR_COMMAND) &&
           (input[APP_FLAME_ADDRESS_OFFSET] == address) &&
           (app_flame_get(&input[APP_FLAME_LENGTH_OFFSET], APP_FLAME_WORD_BYTES) ==
            APP_FLAME_CLEAR_PAYLOAD_BYTES) &&
           (app_flame_crc(input, APP_FLAME_CLEAR_CRC_OFFSET) ==
            app_flame_get(&input[APP_FLAME_CLEAR_CRC_OFFSET], APP_FLAME_WORD_BYTES));
}
