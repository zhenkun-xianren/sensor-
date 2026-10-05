/* 基于本地驱动库 Modbus/RTU主站_RTU_RS485 的请求、CRC 和回显校验适配。 */
#include "port_modbus.h"
#include "dev_modbus.h"
#include "bsp_rs485.h"

static DevModbus s_modbus; // 对 APP 提供的静态协议设备对象。
static uint8_t s_initialized; // 协议设备是否已完成初始化。
static uint8_t s_in_transaction; // 防止同一设备重复进入事务。

/**
 * @brief 计算 Modbus CRC-16，线上低字节先传输。
 * @param data 待计算的数据。
 * @param length 数据字节数。
 * @return 初值 FFFF、多项式 A001 得到的 CRC。
 */
static uint16_t modbus_crc16(const uint8_t *data, uint8_t length)
{
    uint16_t crc = PORT_MODBUS_CRC_INITIAL_VALUE; // 本次逐位计算的 CRC。
    uint8_t byteIndex; // 当前参与校验的字节下标。
    uint8_t bitIndex; // 当前字节的位循环下标。
    for (byteIndex = 0U; byteIndex < length; ++byteIndex) {
        crc ^= data[byteIndex];
        for (bitIndex = 0U; bitIndex < PORT_MODBUS_CRC_BITS_PER_BYTE; ++bitIndex) {
            crc = (crc & 1U) != 0U ?
                (uint16_t)((crc >> 1U) ^ PORT_MODBUS_CRC_POLYNOMIAL) :
                (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

/**
 * @brief 初始化底层 RS485 传输。
 * @param self 静态 Modbus 设备对象。
 * @return 成功返回 OK，对象错误返回 ARGUMENT，底层失败返回 IO。
 */
static DevModbusStatus modbus_init(DevModbus *self)
{
    if (self != &s_modbus) {
        return DEV_MODBUS_ERROR_ARGUMENT;
    }
    if (s_in_transaction != 0U) {
        return DEV_MODBUS_ERROR_IO;
    }
    s_initialized = 0U;
    if (BSP_Rs485_Init() != BSP_RS485_OK) {
        return DEV_MODBUS_ERROR_IO;
    }
    s_initialized = 1U;
    return DEV_MODBUS_OK;
}

/**
 * @brief 组装请求并校验有帧边界的完整应答。
 * @param slaveAddress 从站地址，范围 1 至 247。
 * @param registerAddress 零基保持寄存器地址。
 * @param value 要写入的无单位 16 位值。
 * @return 返回发送、接收或协议校验结果。
 */
static DevModbusStatus modbus_execute_write(uint8_t slaveAddress,
    uint16_t registerAddress, uint16_t value)
{
    uint8_t request[PORT_MODBUS_REQUEST_LENGTH]; // 请求帧，包含六字节数据和两字节 CRC。
    uint8_t response[PORT_MODBUS_RESPONSE_LENGTH]; // 完整正常或异常应答。
    uint8_t responseLength = 0U; // 实际收到的完整帧长度。
    uint8_t index; // 请求与正常回显的比较下标。
    uint16_t crc; // 请求或应答的 CRC。
    BspRs485Status result; // 板级传输结果。

    request[0] = slaveAddress;
    request[1] = PORT_MODBUS_WRITE_SINGLE_REGISTER;
    request[2] = (uint8_t)(registerAddress >> 8U);
    request[3] = (uint8_t)registerAddress;
    request[4] = (uint8_t)(value >> 8U);
    request[5] = (uint8_t)value;
    crc = modbus_crc16(request, PORT_MODBUS_CRC_DATA_LENGTH);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8U);
    result = BSP_Rs485_Send(request, PORT_MODBUS_REQUEST_LENGTH);
    if (result != BSP_RS485_OK) {
        return result == BSP_RS485_TIMEOUT ? DEV_MODBUS_ERROR_TIMEOUT : DEV_MODBUS_ERROR_IO;
    }
    result = BSP_Rs485_ReadFrame(response, PORT_MODBUS_RESPONSE_LENGTH,
        &responseLength, PORT_MODBUS_RESPONSE_TIMEOUT_MS);
    if (result == BSP_RS485_TIMEOUT) {
        return DEV_MODBUS_ERROR_TIMEOUT;
    }
    if (result == BSP_RS485_FRAME_ERROR) {
        return DEV_MODBUS_ERROR_RESPONSE;
    }
    if (result != BSP_RS485_OK) {
        return DEV_MODBUS_ERROR_IO;
    }
    if (responseLength != PORT_MODBUS_RESPONSE_LENGTH &&
        responseLength != PORT_MODBUS_EXCEPTION_LENGTH) {
        return DEV_MODBUS_ERROR_RESPONSE;
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
        return response[1] == (PORT_MODBUS_WRITE_SINGLE_REGISTER |
            PORT_MODBUS_EXCEPTION_FUNCTION_MASK) ?
            DEV_MODBUS_ERROR_EXCEPTION : DEV_MODBUS_ERROR_RESPONSE;
    }
    for (index = 0U; index < PORT_MODBUS_RESPONSE_LENGTH; ++index) {
        if (response[index] != request[index]) {
            return DEV_MODBUS_ERROR_RESPONSE;
        }
    }
    return DEV_MODBUS_OK;
}

/**
 * @brief 检查调用契约并执行一个有界写事务。
 * @param self 静态 Modbus 设备对象，调用者须保证单任务独占。
 * @param slaveAddress 从站地址，范围 1 至 247。
 * @param registerAddress 零基保持寄存器地址。
 * @param value 要写入的 16 位值。
 * @return 返回初始化、参数、传输或协议结果。
 */
static DevModbusStatus modbus_write_holding_register(DevModbus *self,
    uint8_t slaveAddress, uint16_t registerAddress, uint16_t value)
{
    DevModbusStatus result; // 本次事务结束时返回的状态。
    if (self != &s_modbus || slaveAddress < PORT_MODBUS_MIN_SLAVE_ADDRESS ||
        slaveAddress > PORT_MODBUS_MAX_SLAVE_ADDRESS) {
        return DEV_MODBUS_ERROR_ARGUMENT;
    }
    if (s_initialized == 0U) {
        return DEV_MODBUS_NOT_INITIALIZED;
    }
    if (s_in_transaction != 0U) {
        return DEV_MODBUS_ERROR_IO;
    }
    s_in_transaction = 1U;
    result = modbus_execute_write(slaveAddress, registerAddress, value);
    s_in_transaction = 0U;
    return result;
}

/**
 * @brief 获取并绑定静态 Modbus 主站设备。
 * @return 静态 Device 对象指针。
 */
DevModbus *GetModbus(void)
{
    s_modbus.init = modbus_init;
    s_modbus.writeHoldingRegister = modbus_write_holding_register;
    return &s_modbus;
}

