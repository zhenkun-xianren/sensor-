#ifndef APP_MAIN_H
#define APP_MAIN_H

/* 默认启动接收/测距模块，模式由app_ultrasonic_range.h选择；置0恢复连续发射参考。 */
#define APP_MAIN_ENABLE_ULTRASONIC_RANGE_TEST 1

/** @brief APP 总启动接口返回的失败原因；正常调度不会返回。 */
typedef enum {
    APP_MAIN_MODULE_FAILED = 1, /**< 功能模块启动失败，未启动调度器。 */
    APP_MAIN_SCHEDULER_RETURNED = 2 /**< 调度器异常返回，需要固件入口停止。 */
} AppMainFailure;

/**
 * @brief 编排模块启动并运行调度器；功能任务的实现和创建仍位于独立模块。
 * @return 正常运行不返回；模块失败返回 APP_MAIN_MODULE_FAILED，
 *         调度器返回时返回 APP_MAIN_SCHEDULER_RETURNED，由固件入口处理。
 */
int AppMain_Start(void);

#endif
