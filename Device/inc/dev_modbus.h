#ifndef DEV_MODBUS_H
#define DEV_MODBUS_H

#include <stdint.h>

typedef struct DevModbus DevModbus;

/**
 * @brief Modbus RTU 请求帧构造结果。
 */
typedef enum {
    DEV_MODBUS_OK = 0, /**< 请求帧构造成功。 */
    DEV_MODBUS_ERROR_ARGUMENT, /**< 设备对象、从站地址或输出参数无效。 */
    DEV_MODBUS_ERROR_BUFFER /**< 输出缓冲区容量不足。 */
} DevModbusStatus;

/**
 * @brief Modbus RTU 单保持寄存器写请求的设备接口。
 */
struct DevModbus {
    /**
     * @brief 构造功能码 0x06 的请求帧，不执行串口发送。
     * @param self 接收请求的设备对象。
     * @param slaveAddress 从站地址，范围为 1 至 247。
     * @param registerAddress 保持寄存器地址。
     * @param value 要写入的 16 位数值。
     * @param frame 保存完整 RTU 请求帧的输出缓冲区。
     * @param capacity 输出缓冲区容量，单位为字节。
     * @param frameLength 保存生成帧长度的输出指针。
     * @return 构造成功返回 DEV_MODBUS_OK，否则返回对应参数或容量错误。
     */
    DevModbusStatus (*buildWriteHoldingRegisterRequest)(DevModbus *self,
                                                        uint8_t slaveAddress,
                                                        uint16_t registerAddress,
                                                        uint16_t value,
                                                        uint8_t *frame,
                                                        uint8_t capacity,
                                                        uint8_t *frameLength);
};

/**
 * @brief 获取静态 Modbus RTU 设备接口。
 * @return 指向 Modbus 设备接口对象的指针。
 */
DevModbus *GetModbus(void);

#endif /* DEV_MODBUS_H */
