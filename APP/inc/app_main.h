#ifndef APP_MAIN_H
#define APP_MAIN_H

/** @brief 全局应用启动失败结果；正常调度不返回。 */
typedef enum {
    APP_MAIN_NOT_STARTED = 0, // 应用启动尚未执行，供固件故障定位使用。
    APP_MAIN_KEY_ERROR = -1, // 按键模块初始化或启动失败。
    APP_MAIN_SCHEDULER_ERROR = -2, // 调度器异常返回。
    APP_MAIN_DISPLAY_ERROR = -3, // 数码管初始化、默认帧提交或扫描启动失败。
    APP_MAIN_RS485_ERROR = -4, // RS485设备、查询任务或周期定时器启动失败。
    APP_MAIN_SETTINGS_ERROR = -5 // Flash保存模块静态资源创建失败。
} AppMainResult;

/** @brief 恢复设定，启动显示、按键、雷达周期查询及唯一FreeRTOS调度器。 @return 仅在失败时返回具名错误。 */
AppMainResult App_Main_Run(void);

#endif
