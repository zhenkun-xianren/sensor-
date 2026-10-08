#ifndef APP_KEY_H
#define APP_KEY_H

#include "dev_key.h"

/* 应用层按键命名；物理标识及底层GPIO映射保持原驱动定义。 */
#define APP_KEY_INCREASE DEV_KEY_SW4 // 加按键：SW4（PA1），仅编辑模式调整阈值。
#define APP_KEY_DECREASE DEV_KEY_SW6 // 减按键：SW6（PA0），仅编辑模式调整阈值。
#define APP_KEY_SET      DEV_KEY_SW5 // 设定按键：SW5（PA2），切换显示、保存和取消错误提示。

/** @brief 按键扫描模块启动结果。 */
typedef enum {
    APP_KEY_OK = 0, // 模块已经成功启动。
    APP_KEY_DEVICE_ERROR = -1, // 获取设备或设备初始化失败。
    APP_KEY_TIMER_ERROR = -2 // 静态定时器创建或启动失败。
} AppKeyResult;

/**
 * @brief 初始化设备并在调度器启动前安排静态周期扫描；重复成功调用不再初始化。
 * @return 成功返回APP_KEY_OK，其余返回对应设备或定时器错误。
 */
AppKeyResult App_Key_Start(void);

#endif
