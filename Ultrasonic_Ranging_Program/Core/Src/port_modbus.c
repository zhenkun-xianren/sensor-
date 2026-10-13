#include "port_modbus.h"
#include "dev_modbus.h"
#include "bsp_rs485.h"

static DevModbus s_modbus; // 保存所选能力的静态设备接口。
static uint8_t s_initialized; // 初始化成功置 1，底层发送失败后清零。

/**
 * @brief 计算 Modbus CRC16，帧中按低字节先发送校验值。
 * @param data 参与校验的字节序列。
 * @param length 参与校验的字节数。
 * @return 初值 0xFFFF、多项式 0xA001 计算的 CRC16。
 */
static uint16_t modbus_crc16(const uint8_t *data, uint8_t length)
{
    uint16_t crc = PORT_MODBUS_CRC_INITIAL_VALUE; // 保存逐位更新的校验结果。
    uint8_t byteIndex; // 遍历参与校验的请求或应答字节。
    for (byteIndex = 0U; byteIndex < length; ++byteIndex) {
        uint8_t bitIndex; // 遍历当前字节的数据位。
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
 * @brief 为只发送和应答校验共用构造功能码 0x06 的完整请求。
 * @param request 至少能容纳 PORT_MODBUS_REQUEST_LENGTH 字节的内部缓冲区。
 * @param slaveAddress 已检查合法范围的从站地址。
 * @param registerAddress 零基保持寄存器地址。
 * @param value 要发送的 16 位寄存器数值。
 */
static void modbus_build_request(uint8_t *request, uint8_t slaveAddress,
                                 uint16_t registerAddress, uint16_t value)
{
    uint16_t crc; // 保存前六个请求字节的校验值。
    request[0] = slaveAddress;
    request[1] = PORT_MODBUS_WRITE_SINGLE_REGISTER;
    request[2] = (uint8_t)(registerAddress >> 8U);
    request[3] = (uint8_t)registerAddress;
    request[4] = (uint8_t)(value >> 8U);
    request[5] = (uint8_t)value;
    crc = modbus_crc16(request, PORT_MODBUS_CRC_DATA_LENGTH);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8U);
}

/**
 * @brief 初始化所选功能的设备并保存明确失败状态。
 * @param self 静态 Modbus 设备对象。
 * @return 成功返回 OK，失败返回 ARGUMENT、IO 或 TIMEOUT。
 */
static DevModbusStatus modbus_init(DevModbus *self)
{
    BspRs485Status result; // 保存板级初始化结果。
    if (self != &s_modbus) {
        return DEV_MODBUS_ERROR_ARGUMENT;
    }
    s_initialized = 0U;
    result = BSP_Rs485_Init();
    if (result == BSP_RS485_TIMEOUT) {
        return DEV_MODBUS_ERROR_TIMEOUT;
    }
    if (result != BSP_RS485_OK) {
        return DEV_MODBUS_ERROR_IO;
    }
    s_initialized = 1U;
    return DEV_MODBUS_OK;
}

/**
 * @brief 共用功能码 0x06 的本地发送流程，不读取从站应答或重试。
 * @param self 静态 Modbus 设备对象。
 * @param slaveAddress 从站地址 1 至 247。
 * @param registerAddress 零基保持寄存器地址。
 * @param value 要发送的 16 位寄存器数值。
 * @return OK 仅表示 UART 已发完，失败返回 ARGUMENT、NOT_INITIALIZED、IO 或 TIMEOUT。
 */
static DevModbusStatus modbus_send_holding_register(DevModbus *self,
                                                   uint8_t slaveAddress,
                                                   uint16_t registerAddress,
                                                   uint16_t value)
{
    uint8_t request[PORT_MODBUS_REQUEST_LENGTH]; // 保存含校验值的完整请求。
    BspRs485Status sendResult; // 保存本地串口发送结果。
    if (self != &s_modbus || slaveAddress < PORT_MODBUS_MIN_SLAVE_ADDRESS ||
        slaveAddress > PORT_MODBUS_MAX_SLAVE_ADDRESS) {
        return DEV_MODBUS_ERROR_ARGUMENT;
    }
    if (s_initialized == 0U) {
        return DEV_MODBUS_NOT_INITIALIZED;
    }
    modbus_build_request(request, slaveAddress, registerAddress, value);
    sendResult = BSP_Rs485_Send(request, PORT_MODBUS_REQUEST_LENGTH);
    if (sendResult != BSP_RS485_OK) {
        /* 不重发失败帧，下一次独立调用必须先重新初始化。 */
        s_initialized = 0U;
        if (sendResult == BSP_RS485_TIMEOUT) {
            return DEV_MODBUS_ERROR_TIMEOUT;
        }
        if (sendResult == BSP_RS485_NOT_INITIALIZED) {
            return DEV_MODBUS_NOT_INITIALIZED;
        }
        return DEV_MODBUS_ERROR_IO;
    }
    return DEV_MODBUS_OK;
}

#if DEV_MODBUS_ENABLE_RESPONSE
/**
 * @brief 在统一应答期限内读取一个 Modbus RTU 字节。
 * @param data 用于保存接收字节的输出指针。
 * @param startTick 请求发送完成时的毫秒计数。
 * @return 字节读取成功返回 OK，超时或底层接收失败返回对应状态。
 */
static DevModbusStatus modbus_read_before_deadline(uint8_t *data, uint32_t startTick)
{
    uint32_t elapsed = (uint32_t)(BSP_Rs485_GetTickMs() - startTick); // 保存已用应答时间，允许自然回绕。
    uint32_t remaining; // 保存本次字节读取允许等待的剩余毫秒数。
    BspRs485Status result; // 保存板级读取结果。
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
    if (result == BSP_RS485_NOT_INITIALIZED) {
        s_initialized = 0U;
        return DEV_MODBUS_NOT_INITIALIZED;
    }
    return DEV_MODBUS_ERROR_IO;
}

/**
 * @brief 保留单寄存器写入与回显、异常应答校验，通过共用流程发送请求。
 * @param self 静态 Modbus 设备对象。
 * @param slaveAddress 从站地址 1 至 247。
 * @param registerAddress 零基保持寄存器地址。
 * @param value 要写入的 16 位寄存器数值。
 * @return 有效回显返回 OK，失败返回参数、初始化、IO、超时、CRC、应答或异常状态。
 */
static DevModbusStatus modbus_write_holding_register(DevModbus *self,
                                                      uint8_t slaveAddress,
                                                      uint16_t registerAddress,
                                                      uint16_t value)
{
    uint8_t request[PORT_MODBUS_REQUEST_LENGTH]; // 保存用于比较回显的请求帧。
    uint8_t response[PORT_MODBUS_RESPONSE_LENGTH]; // 保存正常或异常应答。
    uint8_t responseLength; // 保存当前功能码对应的应答长度。
    uint8_t index; // 遍历剩余应答字节并比较回显。
    uint16_t crc; // 保存应答数据计算得到的校验值。
    uint32_t responseStartTick; // 保存完整应答期限起点。
    DevModbusStatus readResult; // 保存共用发送或字节读取结果。
    if (self != &s_modbus || slaveAddress < PORT_MODBUS_MIN_SLAVE_ADDRESS ||
        slaveAddress > PORT_MODBUS_MAX_SLAVE_ADDRESS) {
        return DEV_MODBUS_ERROR_ARGUMENT;
    }
    if (s_initialized == 0U) {
        return DEV_MODBUS_NOT_INITIALIZED;
    }
    BSP_Rs485_ClearRx();
    readResult = modbus_send_holding_register(self, slaveAddress, registerAddress, value);
    if (readResult != DEV_MODBUS_OK) {
        return readResult;
    }
    /* 发送已完成后，以同一组帧函数建立回显期望，避免维护两份字节布局。 */
    responseStartTick = BSP_Rs485_GetTickMs();
    modbus_build_request(request, slaveAddress, registerAddress, value);
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
#endif

/**
 * @brief 按集中功能宏绑定静态设备，不访问硬件或重置设备状态。
 * @return 单实例主站设备对象。
 */
DevModbus *GetModbus(void)
{
    s_modbus.init = modbus_init;
#if DEV_MODBUS_ENABLE_SEND
    s_modbus.sendHoldingRegister = modbus_send_holding_register;
#endif
#if DEV_MODBUS_ENABLE_RESPONSE
    s_modbus.writeHoldingRegister = modbus_write_holding_register;
#endif
    return &s_modbus;
}
