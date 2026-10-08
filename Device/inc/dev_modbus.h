#ifndef DEV_MODBUS_H
#define DEV_MODBUS_H

#include <stdint.h>
#include "dev_modbus_config.h"

/** @brief 单实例 Modbus RTU 主站设备，仅允许一个任务串行调用。 */
typedef struct DevModbus DevModbus;

/** @brief 本地操作或应答校验结果；发送成功不表示从站已执行。 */
typedef enum {
    DEV_MODBUS_OK = 0,            /**< 初始化、本地发送或应答校验成功，含义由调用接口决定。 */
    DEV_MODBUS_ERROR_ARGUMENT = 1, /**< 对象无效或从站地址不在 1 至 247 范围。 */
    DEV_MODBUS_ERROR_IO = 2,      /**< 板级配置、串口操作或接收缓冲区失败。 */
    DEV_MODBUS_ERROR_TIMEOUT = 3, /**< 本地发送或等待应答超过规定期限。 */
    DEV_MODBUS_ERROR_CRC = 4,     /**< 从站应答 CRC 校验失败。 */
    DEV_MODBUS_ERROR_RESPONSE = 5, /**< 应答地址、功能码或回显数据不匹配。 */
    DEV_MODBUS_ERROR_EXCEPTION = 6, /**< 从站返回有效的 Modbus 异常应答。 */
    DEV_MODBUS_NOT_INITIALIZED = 7 /**< 初始化未完成或发送失败后需要重新初始化。 */
} DevModbusStatus;

/** @brief 无芯片和 RTOS 依赖的主站契约，能力由集中功能宏选择。 */
struct DevModbus {
#if DEV_MODBUS_ENABLE_READ
    /**
     * @brief 读取1至125个保持寄存器，完整校验成功后才写入输出数组，不自动重试。
     * @param self 单实例设备对象，仅允许一个任务串行调用。
     * @param slaveAddress 从站地址1至247，不支持广播。
     * @param registerAddress 零基首地址，末地址不得超过0xFFFF。
     * @param registerCount 寄存器数量1至125。
     * @param values 至少容纳registerCount个uint16_t的输出数组；失败时保持不变。
     * @return 成功返回OK，失败返回参数、未初始化、IO、超时、CRC、应答或异常状态。
     */
    DevModbusStatus (*readHoldingRegisters)(DevModbus *self, uint8_t slaveAddress,
        uint16_t registerAddress, uint16_t registerCount, uint16_t *values);
#endif
    /**
     * @brief 初始化所选功能需要的 RS485 串口，调用前应完成基础时钟和计时初始化。
     * @param self GetModbus 返回的静态设备对象。
     * @return 成功返回 OK；对象错误返回 ARGUMENT，底层失败返回 IO 或 TIMEOUT。
     */
    DevModbusStatus (*init)(DevModbus *self);

#if DEV_MODBUS_ENABLE_SEND
    /**
     * @brief 发送功能码 0x06 请求，不接收、不等待应答、不重试。
     * @param self GetModbus 返回的设备对象，仅允许任务上下文串行调用。
     * @param slaveAddress 从站地址 1 至 247，不支持广播地址 0。
     * @param registerAddress 协议零基地址 0x0000 至 0xFFFF，不使用 40001 编号。
     * @param value 要发送的 16 位数值，范围为 0x0000 至 0xFFFF。
     * @return OK 只表示最后停止位已发送；失败返回 ARGUMENT、NOT_INITIALIZED、IO 或 TIMEOUT。
     */
    DevModbusStatus (*sendHoldingRegister)(DevModbus *self, uint8_t slaveAddress,
                                          uint16_t registerAddress, uint16_t value);
#endif

#if DEV_MODBUS_ENABLE_WRITE
    /**
     * @brief 写入一个保持寄存器并校验功能码 0x06 的回显应答，不自动重试。
     * @param self GetModbus 返回的设备对象，仅允许任务上下文串行调用。
     * @param slaveAddress 从站地址 1 至 247，不支持广播地址 0。
     * @param registerAddress 协议零基地址 0x0000 至 0xFFFF，不使用 40001 编号。
     * @param value 要写入的 16 位数值，范围为 0x0000 至 0xFFFF。
     * @return 有效回显返回 OK；失败返回 ARGUMENT、NOT_INITIALIZED、IO、TIMEOUT、CRC、RESPONSE 或 EXCEPTION。
     */
    DevModbusStatus (*writeHoldingRegister)(DevModbus *self,
                                           uint8_t slaveAddress,
                                           uint16_t registerAddress,
                                           uint16_t value);
#endif
};

/**
 * @brief 获取并绑定静态设备，不执行硬件初始化或重置初始化状态。
 * @return 指向单实例主站设备对象的非空指针。
 */
DevModbus *GetModbus(void);

#endif /* DEV_MODBUS_H */
