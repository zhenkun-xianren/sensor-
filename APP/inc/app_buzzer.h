#ifndef APP_BUZZER_H
#define APP_BUZZER_H

#include <stdint.h>

/**
 * @brief 初始化蜂鸣器设备并创建周期控制所需的软件定时器。
 * @return 初始化成功返回 1；失败返回 0。
 */
uint8_t AppBuzzer_Init(void);

/**
 * @brief 启动蜂鸣器的 1 秒周期响叫，每周期响 500 毫秒、停 500 毫秒。
 * @return 定时器启动命令入队成功返回 1；失败返回 0。
 */
uint8_t AppBuzzer_Start(void);

/**
 * @brief 停止蜂鸣器周期响叫并立即关闭蜂鸣器。
 * @return 定时器停止命令入队成功返回 1；失败返回 0。
 */
uint8_t AppBuzzer_Stop(void);

#endif /* APP_BUZZER_H */
