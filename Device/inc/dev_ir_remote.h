#ifndef DEV_IR_REMOTE_H
#define DEV_IR_REMOTE_H
#include <stdint.h>
/** @brief 红外设备事件读取结果。 */
typedef enum {
    DEV_IR_ERROR = -1, /**< 参数无效或设备尚未初始化。 */
    DEV_IR_EMPTY = 0, /**< 尚无有效按键事件。 */
    DEV_IR_EVENT = 1 /**< 已取出一个完整按键事件。 */
} DevIrResult;
/** @brief 与板级接线无关的 NEC 按键事件。 */
typedef struct {
    uint16_t address; /**< 普通 NEC 为8位地址，扩展 NEC 为16位地址。 */
    uint8_t command; /**< NEC 命令字节，范围0至255。 */
    uint8_t repeat; /**< 1表示重复帧，0表示完整帧。 */
    uint32_t raw_code; /**< 按接收顺序、最低位先行组成的32位原始帧。 */
} IrKeyEvent;
/** @brief 与 MCU 和 RTOS 无关的红外设备契约。 */
typedef struct DevIrRemote DevIrRemote;
struct DevIrRemote {
    /**
     * @brief 初始化红外接收设备和事件缓冲。
     * @param self 接收请求的红外设备对象。
     * @return 成功返回1，参数或板级初始化失败返回0。
     */
    uint8_t (*init)(DevIrRemote *self);
    /**
     * @brief 非阻塞读取下一个有效按键事件。
     * @param self 接收请求的红外设备对象。
     * @param event 接收事件的非空输出指针。
     * @return DEV_IR_EVENT表示成功，DEV_IR_EMPTY表示等待，DEV_IR_ERROR表示失败。
     */
    DevIrResult (*read)(DevIrRemote *self, IrKeyEvent *event);
};
/**
 * @brief 获取红外设备接口对象。
 * @return 指向已绑定方法的静态设备对象。
 */
DevIrRemote *GetIrRemote(void);
#endif
