#ifndef DEV_IR_REMOTE_H
#define DEV_IR_REMOTE_H

#include <stdint.h>

/**
 * @brief 红外遥控器加减键的抽象动作。
 */
typedef enum {
    IR_KEY_ACTION_NONE = 0U,       /**< 当前按键不改变显示数值。 */
    IR_KEY_ACTION_INCREMENT,      /**< 当前显示数值加一。 */
    IR_KEY_ACTION_DECREMENT       /**< 当前显示数值减一。 */
} IrKeyAction;

/**
 * @brief NEC 红外遥控器解码得到的按键事件。
 */
typedef struct {
    uint16_t address;   /**< 遥控器地址；普通地址为 8 位，扩展地址为 16 位。 */
    uint8_t command;    /**< NEC 命令字节。 */
    int8_t digit;       /**< 数字键对应 0 至 9；非数字键为 -1。 */
    uint8_t repeat;     /**< 非零表示 NEC 重复帧，零表示完整按键帧。 */
    uint32_t raw_code;  /**< 解码得到的 32 位 NEC 原始数据；重复帧沿用上次完整帧。 */
    IrKeyAction action; /**< 加减键的抽象动作，其他按键为 IR_KEY_ACTION_NONE。 */
} IrKeyEvent;

/**
 * @brief 提供 NEC 红外解码器初始化和按键事件读取能力的设备接口。
 */
typedef struct DevIrRemote {
    /**
     * @brief 初始化红外接收器、NEC 解码器和事件队列。
     * @param self 接收初始化请求的红外遥控设备对象。
     * @return 初始化成功返回 1，板级输入或协议层初始化失败返回 0。
     */
    uint8_t (*init)(struct DevIrRemote *self);

    /**
     * @brief 从事件队列读取一个已解码的按键事件。
     * @param self 接收读取请求的红外遥控设备对象。
     * @param event 用于接收按键事件的输出对象，不得为空指针。
     * @return 取出事件返回 1；队列为空、参数无效或设备未初始化返回 0。
     */
    uint8_t (*readEvent)(struct DevIrRemote *self, IrKeyEvent *event);
} DevIrRemote;

/**
 * @brief 获取静态 NEC 红外遥控设备接口。
 * @return 指向红外遥控设备接口的指针。
 */
DevIrRemote *GetIrRemote(void);

#endif /* DEV_IR_REMOTE_H */
