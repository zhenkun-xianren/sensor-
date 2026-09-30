#ifndef APP_BUZZER_H
#define APP_BUZZER_H

#include <stdint.h>

#define APP_BUZZER_DEMO_PENDING   0U
#define APP_BUZZER_DEMO_RUNNING   1U
#define APP_BUZZER_DEMO_COMPLETE  2U
#define APP_BUZZER_DEMO_ERROR     3U

extern volatile uint8_t g_app_buzzer_demo_status; // 上电蜂鸣器验证的运行状态，供 SWD 观察。

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

/**
 * @brief 上电后鸣叫两次并自动停止，验证蜂鸣器开关能力。
 * @return 启动两个软件定时器成功返回 1，否则返回 0。
 */
uint8_t AppBuzzer_StartBootDemo(void);

#endif /* APP_BUZZER_H */
