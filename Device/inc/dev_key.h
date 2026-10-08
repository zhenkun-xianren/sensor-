#ifndef DEV_KEY_H
#define DEV_KEY_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 三路物理按键标识，不规定业务用途。 */
typedef enum {
    DEV_KEY_SW6 = 0, /**< 板上SW6。 */
    DEV_KEY_SW4 = 1, /**< 板上SW4。 */
    DEV_KEY_SW5 = 2, /**< 板上SW5。 */
    DEV_KEY_COUNT = 3 /**< 按键数量，不作为有效按键标识。 */
} DevKeyId;

/** @brief 按键操作结果；事件溢出时扫描仍更新消抖状态。 */
typedef enum {
    DEV_KEY_OK = 0, /**< 本次操作成功。 */
    DEV_KEY_NO_EVENT = 1, /**< 当前没有可读取事件。 */
    DEV_KEY_INVALID_ARGUMENT = -1, /**< 对象、按键标识或输出指针无效。 */
    DEV_KEY_NOT_INITIALIZED = -2, /**< 初始化尚未成功。 */
    DEV_KEY_BOARD_ERROR = -3, /**< 板级配置或读取失败。 */
    DEV_KEY_OVERFLOW = -4 /**< 新事件被丢弃；读取时也会报告一次丢失提示。 */
} DevKeyResult;

/** @brief 一次按住最多产生一个长按事件，长按释放不再产生短按事件。 */
typedef enum {
    DEV_KEY_EVENT_PRESS = 0, /**< 按下消抖通过。 */
    DEV_KEY_EVENT_RELEASE = 1, /**< 释放消抖通过。 */
    DEV_KEY_EVENT_SHORT = 2, /**< 未长按的完整按下和释放。 */
    DEV_KEY_EVENT_LONG = 3 /**< 持续按住达到长按阈值。 */
} DevKeyEventType;

/** @brief 固定长度队列中的一条按键事件。 */
typedef struct {
    DevKeyId id; /**< 对应的物理按键标识。 */ // 事件所属物理按键。
    DevKeyEventType type; /**< 消抖后的事件类型。 */ // 保存事件种类。
    uint32_t timestamp_ms; /**< 毫秒时间戳，按uint32_t自然回绕。 */ // 保存事件确认时间。
} DevKeyEvent;

/** @brief 与芯片、GPIO和RTOS无关的按键设备契约。 */
typedef struct DevKey DevKey;
struct DevKey {
    /**
     * @brief 初始化全部按键，清空队列并从释放状态开始消抖。
     * @param self GetKey返回的设备对象；重新初始化须由调用者串行安排。
     * @return 成功返回OK；对象错误返回INVALID_ARGUMENT，硬件失败返回BOARD_ERROR。
     */
    DevKeyResult (*init)(DevKey *self);
    /**
     * @brief 执行一次非阻塞扫描，须由同一个扫描源按约定周期调用。
     * @param self GetKey返回的设备对象。
     * @param now_ms 单调毫秒时间，允许uint32_t自然回绕，不允许倒退。
     * @return 成功返回OK；无效对象、未初始化、硬件失败或事件丢失返回对应结果。
     */
    DevKeyResult (*scan)(DevKey *self, uint32_t now_ms);
    /**
     * @brief 读取最早一条事件，线程间通过驱动内部短临界区串行访问。
     * @param self GetKey返回的设备对象。
     * @param event 非空输出指针；失败或NO_EVENT时不改写。
     * @return 成功返回OK；无事件返回NO_EVENT；OVERFLOW先报告一次丢失，下一次可继续取队列。
     */
    DevKeyResult (*read)(DevKey *self, DevKeyEvent *event);
    /**
     * @brief 查询消抖后的按下状态，不代替事件队列。
     * @param self GetKey返回的设备对象。
     * @param id SW6、SW4或SW5；COUNT无效。
     * @param pressed 非空输出指针；true表示已消抖按下，失败时保持原值。
     * @return 成功返回OK；参数无效或未初始化返回对应结果。
     */
    DevKeyResult (*get_state)(DevKey *self, DevKeyId id, bool *pressed);
};

/**
 * @brief 获取静态按键设备对象并绑定全部方法，不执行硬件初始化。
 * @return 返回唯一的设备对象。
 */
DevKey *GetKey(void);

#endif
