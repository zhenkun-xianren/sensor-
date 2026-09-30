#include "port_modbus.h"
#include "bsp_rs485.h"

#define PORT_MODBUS_TX_TIMEOUT_MS  100UL

/**
 * @brief 初始化 Modbus RTU 端口使用的 USART2 RS485 接口。
 * @return 初始化成功返回 1，底层串口配置失败返回 0。
 */
uint8_t PORT_Modbus_Init(void)
{
    return BSP_RS485_Init();
}

/**
 * @brief 构造并发送功能码 0x06 的单寄存器写请求，不等待从站应答。
 * @param slaveAddress 从站地址，范围为 1 至 247。
 * @param registerAddress 目标保持寄存器地址。
 * @param value 写入的 16 位数值。
 * @return 完整请求帧发送成功返回 1，参数或底层发送失败返回 0。
 */
uint8_t PORT_Modbus_WriteHoldingRegister(uint8_t slaveAddress,
                                         uint16_t registerAddress,
                                         uint16_t value)
{
    uint8_t request[PORT_MODBUS_REQUEST_LENGTH]; // 请求帧缓冲区，包含数据及 CRC。
    uint8_t request_length; // 请求帧生成后的有效长度。

    if (PORT_Modbus_BuildWriteSingleRegisterRequest(slaveAddress,
                                                    registerAddress,
                                                    value,
                                                    request,
                                                    sizeof(request),
                                                    &request_length) == 0U) {
        return 0U;
    }
    return BSP_RS485_Send(request, request_length,
                          PORT_MODBUS_TX_TIMEOUT_MS);
}

/**
 * @brief 计算 Modbus RTU 使用的 CRC-16。
 * @param data 待校验数据。
 * @param length 参与计算的字节数。
 * @return 按 Modbus CRC-16 多项式计算出的校验值。
 */
static uint16_t port_modbus_crc16(const uint8_t *data, uint8_t length)
{
    uint16_t crc = PORT_MODBUS_CRC_INITIAL_VALUE; // CRC-16/Modbus 的初始值。
    uint8_t byte_index; // 当前参与计算的数据字节索引。

    for (byte_index = 0U; byte_index < length; ++byte_index) {
        uint8_t bit_index; // 当前数据字节内参与计算的位索引。

        crc ^= data[byte_index];
        for (bit_index = 0U; bit_index < PORT_MODBUS_CRC_BITS_PER_BYTE;
             ++bit_index) {
            if ((crc & 1U) != 0U) {
                crc = (uint16_t)((crc >> 1U) ^ PORT_MODBUS_CRC_POLYNOMIAL);
            } else {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

/**
 * @brief 构造 Modbus RTU 功能码 0x06 的单寄存器写请求帧。
 * @param slaveAddress 从站地址，范围为 1 至 247。
 * @param registerAddress 目标保持寄存器地址。
 * @param value 写入的 16 位数值。
 * @param frame 用于保存 8 字节 RTU 请求帧的输出缓冲区。
 * @param capacity 输出缓冲区容量，单位为字节。
 * @param frameLength 用于保存生成帧长度的输出指针。
 * @return 请求帧构造成功返回 1；地址或输出参数无效、容量不足时返回 0。
 */
uint8_t PORT_Modbus_BuildWriteSingleRegisterRequest(uint8_t slaveAddress,
                                                    uint16_t registerAddress,
                                                    uint16_t value,
                                                    uint8_t *frame,
                                                    uint8_t capacity,
                                                    uint8_t *frameLength)
{
    uint16_t crc; // 请求帧前六个字节对应的 Modbus CRC-16。

    if (slaveAddress < PORT_MODBUS_MIN_SLAVE_ADDRESS ||
        slaveAddress > PORT_MODBUS_MAX_SLAVE_ADDRESS ||
        frame == 0 || frameLength == 0) {
        return 0U;
    }
    if (capacity < PORT_MODBUS_REQUEST_LENGTH) {
        return 0U;
    }

    frame[0] = slaveAddress;
    frame[1] = PORT_MODBUS_FUNCTION_WRITE_SINGLE;
    frame[2] = (uint8_t)(registerAddress >> 8U);
    frame[3] = (uint8_t)registerAddress;
    frame[4] = (uint8_t)(value >> 8U);
    frame[5] = (uint8_t)value;
    crc = port_modbus_crc16(frame, PORT_MODBUS_CRC_DATA_LENGTH);
    frame[6] = (uint8_t)crc;
    frame[7] = (uint8_t)(crc >> 8U);
    *frameLength = PORT_MODBUS_REQUEST_LENGTH;
    return 1U;
}
