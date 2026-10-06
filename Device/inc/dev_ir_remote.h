#ifndef DEV_IR_REMOTE_H
#define DEV_IR_REMOTE_H

#include <stdint.h>

/** @brief 红外设备调用结果。 */
typedef enum {
    DEV_IR_OK = 0,                 /**< 操作成功，读取时表示获得一条事件。 */
    DEV_IR_NO_EVENT = 1,           /**< 已初始化但事件队列为空。 */
    DEV_IR_ERROR_ARGUMENT = -1,    /**< 对象或输出指针无效。 */
    DEV_IR_ERROR_NOT_READY = -2,   /**< 尚未成功初始化设备。 */
    DEV_IR_ERROR_INIT = -3         /**< 板级时钟、资源占用或配置核对失败。 */
} DevIrResult;

/** @brief 驱动库键码表提供的抽象动作；驱动不会执行对应业务。 */
typedef enum {
    IR_KEY_ACTION_NONE = 0U,       /**< 无加减映射。 */
    IR_KEY_ACTION_INCREMENT,      /**< 库中加一键映射。 */
    IR_KEY_ACTION_DECREMENT       /**< 库中减一键映射。 */
} IrKeyAction;

/** @brief NEC 完整帧或有效重复帧产生的事件。 */
typedef struct {
    uint16_t address;   /**< 标准地址0至255，扩展地址0至65535。 */ // 解码后的遥控地址。
    uint8_t command;    /**< 命令字节0至255。 */ // 保存命令，不触发控制动作。
    int8_t digit;       /**< 库中数字键映射0至9，其他命令为-1。 */ // 保存数字键提示。
    uint8_t repeat;     /**< 完整帧为0，重复帧为1。 */ // 区分完整按键和长按重复。
    uint32_t raw_code;  /**< 按LSB先行组装的32位帧；重复帧沿用最近完整帧。 */ // 保存原始帧。
    IrKeyAction action; /**< 库中加减映射，不执行应用动作。 */ // 保存库中动作提示。
} IrKeyEvent;

/** @brief 与芯片及操作系统无关的红外设备对象。 */
typedef struct DevIrRemote DevIrRemote;

/** @brief 红外初始化、事件读取与队列诊断契约；调用方须串行调用初始化。 */
struct DevIrRemote {
    /**
     * @brief 显式初始化红外接收；再次成功调用会清空事件与统计。
     * @param self GetIrRemote返回的设备对象。
     * @return 成功为DEV_IR_OK，参数错误为DEV_IR_ERROR_ARGUMENT，板级失败为DEV_IR_ERROR_INIT。
     */
    DevIrResult (*init)(DevIrRemote *self); // 初始化方法。
    /**
     * @brief 非阻塞取出下一条事件；失败时不修改输出。
     * @param self GetIrRemote返回的设备对象。
     * @param event 接收事件的非空输出指针。
     * @return 成功为DEV_IR_OK，空队列为DEV_IR_NO_EVENT，未初始化为DEV_IR_ERROR_NOT_READY，参数错误为DEV_IR_ERROR_ARGUMENT。
     */
    DevIrResult (*read_event)(DevIrRemote *self, IrKeyEvent *event); // 读取方法。
    /**
     * @brief 读取队列满时丢弃的新事件数量。
     * @param self GetIrRemote返回的设备对象。
     * @param count 输出累计次数；达到UINT32_MAX后饱和，成功初始化时清零。
     * @return 成功为DEV_IR_OK，未初始化为DEV_IR_ERROR_NOT_READY，参数错误为DEV_IR_ERROR_ARGUMENT。
     */
    DevIrResult (*get_overflow_count)(DevIrRemote *self, uint32_t *count); // 队列溢出诊断方法。
};

/**
 * @brief 获取静态设备对象；只绑定接口，不访问或启用硬件。
 * @return 有效静态设备对象指针。
 */
DevIrRemote *GetIrRemote(void);

#endif
