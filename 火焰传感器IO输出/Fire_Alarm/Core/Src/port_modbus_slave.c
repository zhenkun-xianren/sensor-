#include <stdbool.h>
#include <stddef.h>
#include "port_modbus_slave.h"
#include "bsp_rs485.h"

static DevModbusSlave s_device; // 保存静态从站方法表。
static bool s_initialized = false; // 标记当前板级实例是否可用。
static uint8_t s_slave_address; // 保存当前从站地址。
static DevModbusReadRegisters s_read_registers; // 保存应用读寄存器回调。
static void *s_context; // 保存由应用维护生命周期的上下文。
static uint8_t s_response[DEV_MODBUS_SLAVE_ADU_CAPACITY]; // 异步发送结束前保持不变的应答缓冲区。
static uint16_t s_values[DEV_MODBUS_SLAVE_MAX_REGISTERS]; // 回调一次返回的连续寄存器快照。

/**
 * @brief 沿用来源包的逐位 CRC16 算法，计数扩展为可覆盖完整 RTU 帧的类型。
 * @param data 待校验的字节序列。
 * @param length 不含 CRC 的字节数。
 * @return Modbus CRC16，在线路上低字节先发送。
 */
static uint16_t modbus_slave_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = PORT_MODBUS_CRC_INITIAL_VALUE; // 保存累计校验值。
    uint16_t byteIndex; // 遍历参与校验的字节。
    for (byteIndex = 0U; byteIndex < length; ++byteIndex) {
        uint8_t bitIndex; // 遍历当前字节中的数据位。
        crc ^= data[byteIndex];
        for (bitIndex = 0U; bitIndex < PORT_MODBUS_CRC_BITS_PER_BYTE; ++bitIndex) {
            if ((crc & PORT_MODBUS_CRC_LOW_BIT) != 0U) {
                crc = (uint16_t)((crc >> PORT_MODBUS_CRC_SHIFT) ^ PORT_MODBUS_CRC_POLYNOMIAL);
            } else {
                crc >>= PORT_MODBUS_CRC_SHIFT;
            }
        }
    }
    return crc;
}

/**
 * @brief 读取网络字节序的寄存器地址或数量。
 * @param data 至少包含一个双字节字段的输入指针。
 * @return 转换后的十六位无符号值。
 */
static uint16_t modbus_slave_read_word(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << PORT_MODBUS_BYTE_SHIFT) | data[1]);
}

/**
 * @brief 初始化静态从站并保存Modbus回调或原始帧邮箱。
 * @param self 静态从站对象。
 * @param slaveAddress 从站地址，范围 1 至 247。
 * @param readRegisters 非空读寄存器回调；NULL选择原始帧模式。
 * @param context Modbus应用上下文或可选原始帧邮箱。
 * @return 成功返回 OK，参数或板级失败返回具名错误。
 */
static DevModbusSlaveStatus modbus_slave_init(DevModbusSlave *self, uint8_t slaveAddress,
                                            DevModbusReadRegisters readRegisters, void *context)
{
    BspRs485Status result; // 保存板级初始化结果。
    if ((self != &s_device) ||
        (slaveAddress < PORT_MODBUS_MIN_ADDRESS) || (slaveAddress > PORT_MODBUS_MAX_ADDRESS)) {
        return DEV_MODBUS_SLAVE_ERROR_ARGUMENT;
    }
    s_initialized = false;
    result = BSP_Rs485_Init();
    if (result != BSP_RS485_OK) {
        return (result == BSP_RS485_TIMEOUT) ? DEV_MODBUS_SLAVE_ERROR_TIMEOUT : DEV_MODBUS_SLAVE_ERROR_IO;
    }
    s_slave_address = slaveAddress;
    s_read_registers = readRegisters;
    s_context = context;
    if ((readRegisters == NULL) && (context != NULL)) {
        DevModbusRawFrame *raw_frame = (DevModbusRawFrame *)context; // 初始化原始接收邮箱。
        raw_frame->length = 0U;
        raw_frame->ready = false;
    }
    s_initialized = true;
    return DEV_MODBUS_SLAVE_OK;
}

/**
 * @brief 为有效本机请求构造正常或异常应答，不计算尾部 CRC。
 * @param request 已通过 CRC 校验的请求帧。
 * @param length 请求总字节数。
 * @return 应答中不含 CRC 的字节数。
 */
static uint16_t modbus_slave_build_response(const uint8_t *request, uint16_t length)
{
    uint8_t functionCode = request[PORT_MODBUS_FUNCTION_INDEX]; // 保存请求功能码。
    uint8_t exception = PORT_MODBUS_EXCEPTION_FUNCTION; // 默认返回不支持的功能码。
    uint16_t quantity = 0U; // 保存通过校验的读取数量。
    uint16_t startAddress; // 保存零基起始地址。
    uint16_t index; // 遍历应用返回的寄存器快照。
    DevModbusRegisterResult result; // 保存回调状态并映射异常应答。
    s_response[PORT_MODBUS_ADDRESS_INDEX] = s_slave_address;
    if ((functionCode == DEV_MODBUS_SLAVE_READ_HOLDING) || (functionCode == DEV_MODBUS_SLAVE_READ_INPUT)) {
        exception = PORT_MODBUS_EXCEPTION_VALUE;
        if (length == PORT_MODBUS_READ_REQUEST_LENGTH) {
            startAddress = modbus_slave_read_word(&request[PORT_MODBUS_START_INDEX]);
            quantity = modbus_slave_read_word(&request[PORT_MODBUS_QUANTITY_INDEX]);
            if ((quantity != 0U) && (quantity <= DEV_MODBUS_SLAVE_MAX_REGISTERS)) {
                if (((uint32_t)startAddress + quantity) > PORT_MODBUS_REGISTER_ADDRESS_SPACE) {
                    exception = PORT_MODBUS_EXCEPTION_ADDRESS;
                } else {
                    result = s_read_registers(s_context, functionCode, startAddress, quantity, s_values);
                    if (result == DEV_MODBUS_REGISTER_OK) {
                        s_response[PORT_MODBUS_FUNCTION_INDEX] = functionCode;
                        s_response[PORT_MODBUS_BYTE_COUNT_INDEX] = (uint8_t)(quantity * PORT_MODBUS_WORD_BYTES);
                        for (index = 0U; index < quantity; ++index) {
                            uint16_t offset = PORT_MODBUS_DATA_INDEX + index * PORT_MODBUS_WORD_BYTES; // 保存当前数据字段位置。
                            s_response[offset] = (uint8_t)(s_values[index] >> PORT_MODBUS_BYTE_SHIFT);
                            s_response[offset + 1U] = (uint8_t)s_values[index];
                        }
                        return (uint16_t)(PORT_MODBUS_DATA_INDEX + quantity * PORT_MODBUS_WORD_BYTES);
                    }
                    if (result == DEV_MODBUS_REGISTER_ILLEGAL_ADDRESS) {
                        exception = PORT_MODBUS_EXCEPTION_ADDRESS;
                    } else if (result == DEV_MODBUS_REGISTER_ILLEGAL_VALUE) {
                        exception = PORT_MODBUS_EXCEPTION_VALUE;
                    } else {
                        exception = PORT_MODBUS_EXCEPTION_FAILURE;
                    }
                }
            }
        }
    }
    s_response[PORT_MODBUS_FUNCTION_INDEX] = functionCode | PORT_MODBUS_EXCEPTION_MASK;
    s_response[PORT_MODBUS_EXCEPTION_INDEX] = exception;
    return PORT_MODBUS_EXCEPTION_DATA_LENGTH;
}

/**
 * @brief 主循环处理至多一个完整请求，原始模式把完整帧交给应用邮箱。
 * @param self 已初始化的静态从站对象。
 * @return 应答提交、空闲、忙或具名失败状态。
 */
static DevModbusSlaveStatus modbus_slave_poll(DevModbusSlave *self)
{
    const uint8_t *request; // 借用板级接收缓冲区，释放前中断不覆盖内容。
    uint16_t length; // 保存当前完整请求长度。
    uint16_t dataLength; // 保存应答中参与 CRC 的长度。
    uint16_t crc; // 保存请求或应答 CRC。
    BspRs485Status result; // 保存板级状态机结果。
    if (self != &s_device) {
        return DEV_MODBUS_SLAVE_ERROR_ARGUMENT;
    }
    if (!s_initialized) {
        return DEV_MODBUS_SLAVE_NOT_INITIALIZED;
    }
    result = BSP_Rs485_Poll();
    if (result == BSP_RS485_BUSY) {
        return DEV_MODBUS_SLAVE_BUSY;
    }
    if (result != BSP_RS485_OK) {
        s_initialized = false;
        return (result == BSP_RS485_TIMEOUT) ? DEV_MODBUS_SLAVE_ERROR_TIMEOUT : DEV_MODBUS_SLAVE_ERROR_IO;
    }
    if (BSP_Rs485_AcquireFrame(&request, &length) != BSP_RS485_OK) {
        return DEV_MODBUS_SLAVE_IDLE;
    }
    if (s_read_registers == NULL) {
        if (s_context != NULL) {
            DevModbusRawFrame *raw_frame = (DevModbusRawFrame *)s_context; // APP拥有的单帧邮箱。
            if (!raw_frame->ready && (length <= DEV_MODBUS_SLAVE_ADU_CAPACITY)) {
                uint16_t index; // 复制本次接收帧，不借出BSP的中断缓冲区。
                for (index = 0U; index < length; ++index) {
                    raw_frame->data[index] = request[index];
                }
                raw_frame->length = length;
                raw_frame->ready = true;
            }
        }
        BSP_Rs485_ReleaseFrame(); /* 满邮箱时丢弃新帧，旧帧保持不变。 */
        return DEV_MODBUS_SLAVE_IDLE;
    }
    if ((length < PORT_MODBUS_MIN_ADU) ||
        (request[PORT_MODBUS_ADDRESS_INDEX] != s_slave_address) ||
        ((request[PORT_MODBUS_FUNCTION_INDEX] & PORT_MODBUS_EXCEPTION_MASK) != 0U)) {
        BSP_Rs485_ReleaseFrame();
        return DEV_MODBUS_SLAVE_IDLE;
    }
    dataLength = length - PORT_MODBUS_CRC_BYTES;
    crc = modbus_slave_crc16(request, dataLength);
    if ((request[dataLength] != (uint8_t)crc) ||
        (request[dataLength + 1U] != (uint8_t)(crc >> PORT_MODBUS_BYTE_SHIFT))) {
        BSP_Rs485_ReleaseFrame();
        return DEV_MODBUS_SLAVE_IDLE;
    }
    dataLength = modbus_slave_build_response(request, length);
    crc = modbus_slave_crc16(s_response, dataLength);
    s_response[dataLength] = (uint8_t)crc;
    s_response[dataLength + 1U] = (uint8_t)(crc >> PORT_MODBUS_BYTE_SHIFT);
    result = BSP_Rs485_StartSend(s_response, dataLength + PORT_MODBUS_CRC_BYTES);
    BSP_Rs485_ReleaseFrame();
    if (result == BSP_RS485_BUSY) {
        return DEV_MODBUS_SLAVE_BUSY;
    }
    if (result != BSP_RS485_OK) {
        s_initialized = false;
        return DEV_MODBUS_SLAVE_ERROR_IO;
    }
    return DEV_MODBUS_SLAVE_OK;
}

/**
 * @brief 从原始帧模式主动提交发送，使用Port缓冲区隔离调用者数据生命周期。
 * @param self 本驱动静态对象，init时必须选用NULL回调。
 * @param data 完整原始协议帧，无隐含封装。
 * @param length 实际帧字节数1至ADU_CAPACITY。
 * @return 提交成功OK，忙时不修改在途缓冲区，其他返回具名错误。
 */
static DevModbusSlaveStatus modbus_slave_send_raw(DevModbusSlave *self, const uint8_t *data, uint16_t length)
{
    DevModbusSlaveStatus status; // poll返回的当前发送或保护间隔状态。
    BspRs485Status result; // 底层是否已接受本次发送。
    uint16_t index; // 复制到稳定静态缓冲区的字节位置。
    if ((self != &s_device) || (data == NULL) ||
        (length < PORT_MODBUS_RAW_MIN_LENGTH) || (length > DEV_MODBUS_SLAVE_ADU_CAPACITY)) {
        return DEV_MODBUS_SLAVE_ERROR_ARGUMENT;
    }
    if (!s_initialized) {
        return DEV_MODBUS_SLAVE_NOT_INITIALIZED;
    }
    if (s_read_registers != NULL) {
        return DEV_MODBUS_SLAVE_ERROR_ARGUMENT; /* 两种协议不能同时占有串口。 */
    }
    if ((s_context != NULL) && ((DevModbusRawFrame *)s_context)->ready) {
        return DEV_MODBUS_SLAVE_BUSY; /* 先由APP处理已经收到的命令。 */
    }
    status = modbus_slave_poll(self);
    if ((status != DEV_MODBUS_SLAVE_OK) && (status != DEV_MODBUS_SLAVE_IDLE)) {
        return status; /* TX或保护期结束前绝不覆盖s_response。 */
    }
    if ((s_context != NULL) && ((DevModbusRawFrame *)s_context)->ready) {
        return DEV_MODBUS_SLAVE_BUSY; /* poll刚接收新帧时不抢先发送旧状态。 */
    }
    for (index = 0; index < length; ++index) {
        s_response[index] = data[index];
    }
    result = BSP_Rs485_StartSend(s_response, length);
    if (result == BSP_RS485_BUSY) {
        return DEV_MODBUS_SLAVE_BUSY;
    }
    if (result != BSP_RS485_OK) {
        s_initialized = false;
        return (result == BSP_RS485_TIMEOUT) ? DEV_MODBUS_SLAVE_ERROR_TIMEOUT : DEV_MODBUS_SLAVE_ERROR_IO;
    }
    return DEV_MODBUS_SLAVE_OK;
}

/**
 * @brief 绑定并返回静态裸机从站接口，不重置运行状态。
 * @return 单实例设备对象。
 */
DevModbusSlave *GetModbusSlave(void)
{
    s_device.init = modbus_slave_init;
    s_device.poll = modbus_slave_poll;
    s_device.sendRaw = modbus_slave_send_raw;
    return &s_device;
}
