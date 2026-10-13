#ifndef DEV_IR_REMOTE_H
#define DEV_IR_REMOTE_H

#include <stdint.h>

/**
 * @brief 红外遥控器加减键的抽象动作。
 */
typedef enum {
    IR_KEY_ACTION_NONE = 0U,      /**< 当前按键不改变显示数值。 */
    IR_KEY_ACTION_INCREMENT,     /**< 当前显示数值加一。 */
    IR_KEY_ACTION_DECREMENT      /**< 当前显示数值减一。 */
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
 * @brief 初始化 NEC 解码状态、事件队列和板级红外接收电路。
 */
void DevIrRemote_Init(void);

/**
 * @brief 从红外遥控事件队列读取一个按键事件。
 * @param event 用于接收事件的输出对象，不得为空指针。
 * @return 成功取出事件返回 1；队列为空或 event 为空指针时返回 0。
 */
uint8_t DevIrRemote_ReadEvent(IrKeyEvent *event);

#endif
