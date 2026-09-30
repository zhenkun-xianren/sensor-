#ifndef APP_IR_RELAY_H
#define APP_IR_RELAY_H

#include <stdint.h>

/**
 * @brief 初始化红外按键提示、数值显示、继电器和光耦输出控制任务。
 * @return 初始化和任务创建成功返回 1；失败返回 0。
 */
uint8_t AppIrRelay_Init(void);

#endif
