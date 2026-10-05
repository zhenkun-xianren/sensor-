#ifndef DEV_MODBUS_H
#define DEV_MODBUS_H
#include <stdint.h>

/** @brief 与板卡无关的 Modbus RTU 主站设备对象。 */
typedef struct DevModbus DevModbus;

/** @brief 单保持寄存器写操作的结果。 */
typedef enum {
    DEV_MODBUS_OK = 0,          /**< 从站正常应答与请求一致。 */
    DEV_MODBUS_ERROR_ARGUMENT,  /**< 对象无效或从站地址不在 1 至 247。 */
    DEV_MODBUS_ERROR_IO,        /**< 收发失败、串口错误、溢出或设备正在使用。 */
    DEV_MODBUS_ERROR_TIMEOUT,   /**< 发送或整体应答等待超时。 */
    DEV_MODBUS_ERROR_CRC,       /**< 完整应答的 CRC 错误。 */
    DEV_MODBUS_ERROR_RESPONSE,  /**< 帧时序、长度、地址、功能码或回显不匹配。 */
    DEV_MODBUS_ERROR_EXCEPTION, /**< 从站返回 CRC 有效的 0x86 异常应答。 */
    DEV_MODBUS_NOT_INITIALIZED  /**< 设备尚未成功初始化。 */
} DevModbusStatus;

/** @brief 单任务独占使用的 RTU 主站契约，只支持功能码 0x06。 */
struct DevModbus {
    /**
     * @brief 初始化设备；必须在调度器启动后的任务上下文调用。
     * @param self 本接口返回的设备对象。
     * @return 成功返回 OK，对象错误返回 ARGUMENT，硬件失败返回 IO。
     */
    DevModbusStatus (*init)(DevModbus *self); // 初始化设备的方法。
    /**
     * @brief 写一个保持寄存器，阻塞等待有界发送及从站应答。
     * @param self 本接口返回的设备对象，不能并发调用。
     * @param slaveAddress 从站地址，范围 1 至 247，不支持广播。
     * @param registerAddress 零基保持寄存器地址，范围 0x0000 至 0xFFFF。
     * @param value 要写入的无单位 16 位值，范围 0 至 65535。
     * @return OK 表示正常回显；其余状态表示参数、初始化、I/O、超时、CRC、应答或异常错误。
     */
    DevModbusStatus (*writeHoldingRegister)(DevModbus *self,
        uint8_t slaveAddress, uint16_t registerAddress, uint16_t value); // 执行单寄存器写事务的方法。
};
/**
 * @brief 获取并绑定静态 Modbus 主站设备对象。
 * @return 静态设备对象指针，不分配堆内存。
 */
DevModbus *GetModbus(void);
#endif

