#include "port_modbus.h"
#include "dev_modbus.h"
#include "bsp_rs485.h"

static DevModbus s_modbus;

/**
 * @brief 计算 Modbus RTU 使用的 CRC-16 校验值。
 * @param data 待校验的帧数据。
 * @param length 参与 CRC 计算的字节数。
 * @return 按 Modbus 多项式计算得到的 16 位 CRC 值。
 */
static uint16_t modbus_crc16(const uint8_t *data, uint8_t length)
{
    uint16_t crc = PORT_MODBUS_CRC_INITIAL_VALUE;
    uint8_t byteIndex;

    for (byteIndex = 0U; byteIndex < length; ++byteIndex) {
        uint8_t bitIndex;
        crc ^= data[byteIndex];
        for (bitIndex = 0U; bitIndex < PORT_MODBUS_CRC_BITS_PER_BYTE; ++bitIndex) {
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
 * @brief 初始化 USART2 RS485 板级传输接口。
 * @param self 接收初始化请求的 Modbus 设备对象。
 * @return 初始化成功返回 DEV_MODBUS_OK，否则返回 DEV_MODBUS_ERROR_IO。
 */
static DevModbusStatus modbus_init(DevModbus *self)
{
    if (self == 0) {
        return DEV_MODBUS_ERROR_ARGUMENT;
    }
    return BSP_Rs485_Init() == BSP_RS485_OK ? DEV_MODBUS_OK : DEV_MODBUS_ERROR_IO;
}

/**
 * @brief 在统一 100 ms 应答期限内读取一个 Modbus RTU 字节。
 * @param data 用于保存接收字节的输出指针。
 * @param startTick 请求发送完成时的毫秒计数。
 * @return 字节读取成功返回 DEV_MODBUS_OK；超时或底层接收错误返回对应状态。
 */
static DevModbusStatus modbus_read_before_deadline(uint8_t *data, uint32_t startTick)
{
    uint32_t elapsed = (uint32_t)(BSP_Rs485_GetTickMs() - startTick);
    uint32_t remaining;
    BspRs485Status result;

    if (elapsed >= PORT_MODBUS_RESPONSE_TIMEOUT_MS) {
        return DEV_MODBUS_ERROR_TIMEOUT;
    }
    remaining = PORT_MODBUS_RESPONSE_TIMEOUT_MS - elapsed;
    result = BSP_Rs485_ReadByte(data, remaining);
    if (result == BSP_RS485_OK) {
        return DEV_MODBUS_OK;
    }
    if (result == BSP_RS485_TIMEOUT) {
        return DEV_MODBUS_ERROR_TIMEOUT;
    }
    return DEV_MODBUS_ERROR_IO;
}

/**
 * @brief 向 Modbus 从站写入一个保持寄存器并校验 RTU 应答。
 * @param self 接收写操作的 Modbus 设备对象。
 * @param slaveAddress Modbus 从站地址，范围为 1 至 247。
 * @param registerAddress 目标保持寄存器地址。
 * @param value 要写入的 16 位寄存器值。
 * @return 回显应答有效返回 DEV_MODBUS_OK；失败时返回对应的参数、I/O、超时、CRC、应答或异常状态。
 */
static DevModbusStatus modbus_write_holding_register(DevModbus *self,
                                                      uint8_t slaveAddress,
                                                      uint16_t registerAddress,
                                                      uint16_t value)
{
    uint8_t request[PORT_MODBUS_REQUEST_LENGTH];
    uint8_t response[PORT_MODBUS_RESPONSE_LENGTH];
    uint8_t responseLength;
    uint8_t index;
    uint16_t crc;
    uint32_t responseStartTick;
    BspRs485Status sendResult;
    DevModbusStatus readResult;

    if (self == 0 || slaveAddress < PORT_MODBUS_MIN_SLAVE_ADDRESS ||
        slaveAddress > PORT_MODBUS_MAX_SLAVE_ADDRESS) {
        return DEV_MODBUS_ERROR_ARGUMENT;
    }

    request[0] = slaveAddress;
    request[1] = PORT_MODBUS_WRITE_SINGLE_REGISTER;
    request[2] = (uint8_t)(registerAddress >> 8U);
    request[3] = (uint8_t)registerAddress;
    request[4] = (uint8_t)(value >> 8U);
    request[5] = (uint8_t)value;
    crc = modbus_crc16(request, PORT_MODBUS_CRC_DATA_LENGTH);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8U);

    BSP_Rs485_ClearRx();
    sendResult = BSP_Rs485_Send(request, PORT_MODBUS_REQUEST_LENGTH);
    if (sendResult != BSP_RS485_OK) {
        return sendResult == BSP_RS485_TIMEOUT ? DEV_MODBUS_ERROR_TIMEOUT : DEV_MODBUS_ERROR_IO;
    }

    responseStartTick = BSP_Rs485_GetTickMs();
    readResult = modbus_read_before_deadline(&response[0], responseStartTick);
    if (readResult != DEV_MODBUS_OK) {
        return readResult;
    }
    readResult = modbus_read_before_deadline(&response[1], responseStartTick);
    if (readResult != DEV_MODBUS_OK) {
        return readResult;
    }

    responseLength = response[1] ==
                     (uint8_t)(PORT_MODBUS_WRITE_SINGLE_REGISTER |
                               PORT_MODBUS_EXCEPTION_FUNCTION_MASK)
                         ? PORT_MODBUS_EXCEPTION_LENGTH
                         : PORT_MODBUS_RESPONSE_LENGTH;
    for (index = 2U; index < responseLength; ++index) {
        readResult = modbus_read_before_deadline(&response[index], responseStartTick);
        if (readResult != DEV_MODBUS_OK) {
            return readResult;
        }
    }

    crc = modbus_crc16(response, (uint8_t)(responseLength - 2U));
    if (response[responseLength - 2U] != (uint8_t)crc ||
        response[responseLength - 1U] != (uint8_t)(crc >> 8U)) {
        return DEV_MODBUS_ERROR_CRC;
    }
    if (response[0] != slaveAddress) {
        return DEV_MODBUS_ERROR_RESPONSE;
    }
    if (responseLength == PORT_MODBUS_EXCEPTION_LENGTH) {
        return DEV_MODBUS_ERROR_EXCEPTION;
    }
    for (index = 0U; index < PORT_MODBUS_RESPONSE_LENGTH; ++index) {
        if (response[index] != request[index]) {
            return DEV_MODBUS_ERROR_RESPONSE;
        }
    }

    return DEV_MODBUS_OK;
}

/**
 * @brief 获取并绑定 Modbus RTU 主站设备接口。
 * @return 指向静态 Modbus 设备接口对象的指针。
 */
DevModbus *GetModbus(void)
{
    s_modbus.init = modbus_init;
    s_modbus.writeHoldingRegister = modbus_write_holding_register;
    return &s_modbus;
}
