#ifndef APP_RS485_H
#define APP_RS485_H

/** @brief 雷达周期查询模块的启动结果。 */
typedef enum {
    APP_RS485_OK = 0, // 设备、静态发送任务和周期定时器启动成功。
    APP_RS485_DEVICE_ERROR = -1, // 设备接口缺失或初始化失败。
    APP_RS485_TASK_ERROR = -2, // 静态发送任务创建失败。
    APP_RS485_TIMER_ERROR = -3 // 静态定时器创建或启动失败。
} AppRs485Result;

/**
 * @brief 初始化RS485并启动每500毫秒一次的四个保持寄存器查询，调度启动时立即首次查询。
 * @return 成功返回APP_RS485_OK，失败返回对应设备、任务或定时器错误。
 */
AppRs485Result App_Rs485_Start(void);

#endif
