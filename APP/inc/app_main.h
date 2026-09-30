#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <stdint.h>

/**
 * @brief 启动已注册的 APP 模块并运行 FreeRTOS 调度器。
 * @return 启动失败或调度器意外返回时返回 0；正常调度不会返回。
 */
uint8_t App_Main_Run(void);

#endif /* APP_MAIN_H */
