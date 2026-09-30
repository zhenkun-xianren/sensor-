#ifndef APP_SYSTEM_H
#define APP_SYSTEM_H

#include <stdint.h>

extern volatile uint32_t g_app_system_heartbeat; // 调度任务每秒递增的诊断心跳，供 SWD 观察。

/**
 * @brief 创建不操作现场外设的系统心跳任务。
 * @return 静态任务创建成功返回 1，失败返回 0。
 */
uint8_t App_System_Start(void);

#endif /* APP_SYSTEM_H */
