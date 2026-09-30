#include "dev_modbus.h"
#include "port_modbus.h"

static DevModbus s_modbus; // 对外提供的静态 Modbus Device 接口。

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
    s_modbus.buildWriteHoldingRegisterRequest = dev_modbus_build_write_request;
    return &s_modbus;
}
