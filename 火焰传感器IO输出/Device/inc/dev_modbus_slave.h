#ifndef DEV_MODBUS_SLAVE_H
#define DEV_MODBUS_SLAVE_H

#include <stdbool.h>
#include <stdint.h>
#include "dev_modbus_slave_config.h"

/** @brief 单实例裸机RS485对象，提供RTU从站或独占原始帧收发模式，只允许主循环串行调用。 */
typedef struct DevModbusSlave DevModbusSlave;

/** @brief 本地驱动状态；启动发送成功不等于主站已经收到应答。 */
typedef enum {
    DEV_MODBUS_SLAVE_OK = 0,              /**< 初始化成功或已提交应答。 */
    DEV_MODBUS_SLAVE_IDLE = 1,            /**< 暂无完整请求，或已静默丢弃无效请求。 */
    DEV_MODBUS_SLAVE_BUSY = 2,            /**< 正在发送、等待帧间隔，或当前请求因总线活动取消。 */
    DEV_MODBUS_SLAVE_ERROR_ARGUMENT = -1, /**< 对象、回调或地址参数无效。 */
    DEV_MODBUS_SLAVE_NOT_INITIALIZED = -2,/**< 尚未初始化或发送超时后需要重新初始化。 */
    DEV_MODBUS_SLAVE_ERROR_IO = -3,       /**< 板级时钟、GPIO 或串口初始化失败。 */
    DEV_MODBUS_SLAVE_ERROR_TIMEOUT = -4   /**< 发送超过按帧长度计算的总期限。 */
} DevModbusSlaveStatus;

/** @brief 寄存器回调结果；错误对应 Modbus 异常码，不直接表示硬件验证结论。 */
typedef enum {
    DEV_MODBUS_REGISTER_OK = 0,              /**< 整个连续区间已填写。 */
    DEV_MODBUS_REGISTER_ILLEGAL_ADDRESS = 2, /**< 区间包含未实现地址。 */
    DEV_MODBUS_REGISTER_ILLEGAL_VALUE = 3,   /**< 请求数据不合法。 */
    DEV_MODBUS_REGISTER_DEVICE_FAILURE = 4  /**< 当前无法提供所请求数据。 */
} DevModbusRegisterResult;

/**
 * @brief 一次读取连续寄存器；应快速返回，不阻塞、不分配内存。
 * @param context 初始化传入的应用上下文，可为空。
 * @param functionCode 功能码，READ_HOLDING 或 READ_INPUT。
 * @param startAddress 协议零基起始地址，范围 0 至 65535。
 * @param quantity 数量，范围 1 至 MAX_REGISTERS。
 * @param values 驱动提供的 quantity 项输出数组；填写主机字节序的无单位原始值。
 * @return 全部数据有效返回 OK；地址、值或数据提供失败返回对应枚举。
 */
typedef DevModbusRegisterResult (*DevModbusReadRegisters)(
    void *context, uint8_t functionCode, uint16_t startAddress,
    uint16_t quantity, uint16_t *values);

/** @brief 原始模式的单帧接收邮箱；仅由主循环中的poll和APP串行访问。 */
typedef struct {
    uint8_t data[DEV_MODBUS_SLAVE_ADU_CAPACITY]; // 保存完整原始接收帧。
    uint16_t length; // 当前帧的实际长度。
    bool ready; // 为真时APP尚未消费，Port不会覆盖该帧。
} DevModbusRawFrame;

/** @brief 无芯片、HAL、RTOS 或动态内存依赖的设备契约。 */
struct DevModbusSlave {
    /**
     * @brief 初始化从站；重新初始化会取消现有事务，须先完成板级基础初始化。
     * @param self GetModbusSlave 返回的设备对象。
     * @param slaveAddress 从站地址 1 至 247，不允许使用广播地址。
     * @param readRegisters 非空时为Modbus回调；NULL选择独占原始帧模式。
     * @param context Modbus模式为回调上下文；原始模式为DevModbusRawFrame指针，
     *                NULL仍选择仅发送并丢弃接收。非空对象须保持有效至下一次初始化。
     * @return 成功返回 OK，参数、底层配置或等待失败返回对应错误状态。
     */
    DevModbusSlaveStatus (*init)(DevModbusSlave *self, uint8_t slaveAddress,
                               DevModbusReadRegisters readRegisters, void *context);
    /**
     * @brief 原始帧模式提交主动发送；不会添加地址、功能码或CRC。
     * @param self 用init空回调成功初始化的本对象；Modbus模式不允许调用。
     * @param data 完整协议帧，函数提交前复制进Port静态缓冲区。
     * @param length 字节数1至ADU_CAPACITY。
     * @return OK表示异步提交，BUSY表示尚未提交；失败返回参数、IO或超时错误。
     */
    DevModbusSlaveStatus (*sendRaw)(DevModbusSlave *self, const uint8_t *data, uint16_t length);
    /**
     * @brief 主循环轮询；Modbus模式处理请求，原始模式维护发送并把完整接收帧交给可选邮箱。
     * @param self GetModbusSlave 返回的已初始化设备对象。
     * @return 已提交应答返回 OK，空闲返回 IDLE，事务进行中返回 BUSY，失败返回对应负值。
     */
    DevModbusSlaveStatus (*poll)(DevModbusSlave *self);
};

/**
 * @brief 获取静态设备对象并绑定方法，不执行硬件初始化。
 * @return 单实例从站对象的非空指针。
 */
DevModbusSlave *GetModbusSlave(void);

#endif
