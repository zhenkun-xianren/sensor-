#include "dev_modbus.h"
#include "port_modbus.h"

static DevModbus s_modbus; // 对外提供的静态 Modbus Device 接口。

/**
 * @brief 初始化 Modbus 设备依赖的 Port 层 RS485 接口。
 * @param self 接收初始化请求的设备对象。
 * @return 初始化成功返回 DEV_MODBUS_OK，否则返回底层 I/O 错误。
 */
static DevModbusStatus dev_modbus_init(DevModbus *self)
{
    if (self != &s_modbus) {
        return DEV_MODBUS_ERROR_ARGUMENT;
    }
    return PORT_Modbus_Init() != 0U ? DEV_MODBUS_OK : DEV_MODBUS_ERROR_IO;
}

/**
 * @brief 将单寄存器写请求交由 Port 层发送，不等待从站应答。
 * @param self 接收写请求的设备对象。
 * @param slaveAddress 从站地址，范围为 1 至 247。
 * @param registerAddress 保持寄存器地址。
 * @param value 要写入的 16 位数值。
 * @return 请求发送成功返回 DEV_MODBUS_OK，否则返回参数或底层 I/O 错误。
 */
static DevModbusStatus dev_modbus_write_holding_register(DevModbus *self,
                                                         uint8_t slaveAddress,
                                                         uint16_t registerAddress,
                                                         uint16_t value)
{
    if (self != &s_modbus || slaveAddress < PORT_MODBUS_MIN_SLAVE_ADDRESS ||
        slaveAddress > PORT_MODBUS_MAX_SLAVE_ADDRESS) {
        return DEV_MODBUS_ERROR_ARGUMENT;
    }
    return PORT_Modbus_WriteHoldingRegister(slaveAddress,
                                             registerAddress,
                                             value) != 0U ?
           DEV_MODBUS_OK : DEV_MODBUS_ERROR_IO;
}

/**
 * @brief 将单寄存器写请求交由 Port 层构造为 RTU 帧。
 * @param self 接收请求的设备对象。
 * @param slaveAddress 从站地址，范围为 1 至 247。
 * @param registerAddress 保持寄存器地址。
 * @param value 要写入的 16 位数值。
 * @param frame 保存完整 RTU 请求帧的输出缓冲区。
 * @param capacity 输出缓冲区容量，单位为字节。
 * @param frameLength 保存生成帧长度的输出指针。
 * @return 构造成功返回 DEV_MODBUS_OK，否则返回对应参数或容量错误。
 */
static DevModbusStatus dev_modbus_build_write_request(DevModbus *self,
                                                      uint8_t slaveAddress,
                                                      uint16_t registerAddress,
                                                      uint16_t value,
                                                      uint8_t *frame,
                                                      uint8_t capacity,
                                                      uint8_t *frameLength)
{
    if (self != &s_modbus || slaveAddress < PORT_MODBUS_MIN_SLAVE_ADDRESS ||
        slaveAddress > PORT_MODBUS_MAX_SLAVE_ADDRESS || frame == 0 ||
        frameLength == 0) {
        return DEV_MODBUS_ERROR_ARGUMENT;
    }
    if (capacity < PORT_MODBUS_REQUEST_LENGTH) {
        return DEV_MODBUS_ERROR_BUFFER;
    }
    if (PORT_Modbus_BuildWriteSingleRegisterRequest(slaveAddress,
                                                     registerAddress,
                                                     value,
                                                     frame,
                                                     capacity,
                                                     frameLength) == 0U) {
        return DEV_MODBUS_ERROR_ARGUMENT;
    }
    return DEV_MODBUS_OK;
}

/**
 * @brief 获取并绑定 Modbus Device 接口。
 * @return 指向静态 Modbus 设备接口对象的指针。
 */
DevModbus *GetModbus(void)
{
    s_modbus.init = dev_modbus_init;
    s_modbus.writeHoldingRegister = dev_modbus_write_holding_register;
    s_modbus.buildWriteHoldingRegisterRequest = dev_modbus_build_write_request;
    return &s_modbus;
}
