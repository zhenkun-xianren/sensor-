#ifndef DEV_MODBUS_H
#define DEV_MODBUS_H

#include <stdint.h>

typedef struct DevModbus DevModbus;

/**
 * @brief Modbus RTU 单寄存器写操作的结果。
 */
typedef enum {
    DEV_MODBUS_OK = 0,            /**< 从站回显与请求一致。 */
    DEV_MODBUS_ERROR_ARGUMENT,    /**< 设备指针或从站地址无效。 */
    DEV_MODBUS_ERROR_IO,          /**< UART 读写或接收缓冲区发生错误。 */
    DEV_MODBUS_ERROR_TIMEOUT,     /**< 等待从站应答超时。 */
    DEV_MODBUS_ERROR_CRC,         /**< 从站应答 CRC 校验失败。 */
    DEV_MODBUS_ERROR_RESPONSE,    /**< 应答地址、功能码或回显数据不匹配。 */
    DEV_MODBUS_ERROR_EXCEPTION,   /**< 从站返回 Modbus 异常应答。 */
    DEV_MODBUS_NOT_INITIALIZED    /**< 模块尚未完成初始化。 */
} DevModbusStatus;

/**
 * @brief 与板卡无关的 Modbus RTU 主站能力接口。
 */
struct DevModbus {
    /**
     * @brief 初始化 Modbus RTU 所需的 RS485 串口。
     * @param self 接收初始化请求的设备对象。
     * @return 初始化成功返回 DEV_MODBUS_OK，底层初始化失败返回 DEV_MODBUS_ERROR_IO。
     */
    DevModbusStatus (*init)(DevModbus *self);

    /**
     * @brief 向从站写入一个保持寄存器并校验功能码 0x06 的回显应答。
     * @param self 接收写操作的设备对象。
     * @param slaveAddress Modbus 从站地址，范围为 1 至 247；广播地址不支持应答校验。
     * @param registerAddress 保持寄存器地址，范围为 0x0000 至 0xFFFF。
     * @param value 写入寄存器的 16 位数值，范围为 0x0000 至 0xFFFF。
     * @return 应答有效返回 DEV_MODBUS_OK；失败时返回对应的参数、I/O、超时、CRC、应答或异常状态。
     */
    DevModbusStatus (*writeHoldingRegister)(DevModbus *self,
                                           uint8_t slaveAddress,
                                           uint16_t registerAddress,
                                           uint16_t value);
};

/**
 * @brief 获取 Modbus RTU 主站设备接口。
 * @return 指向静态 Modbus 设备接口对象的指针。
 */
DevModbus *GetModbus(void);

#endif /* DEV_MODBUS_H */
