#ifndef APP_MAIN_H
#define APP_MAIN_H
/** @brief APP启动结果，不暴露GPIO及寄存器细节。 */
typedef enum {
    APP_OK = 0, /**< 应用模块操作成功。 */
    APP_ERROR_GPIO, /**< GPIO设备初始化失败。 */
    APP_ERROR_ACTUATOR, /**< 执行器初始化或状态读取失败。 */
    APP_ERROR_DISPLAY, /**< 显示初始化失败。 */
    APP_ERROR_MODBUS, /**< Modbus初始化失败。 */
    APP_ERROR_IR, /**< 红外初始化失败。 */
    APP_ERROR_TIMER, /**< 应用软件定时器创建或启动失败。 */
    APP_ERROR_TASK, /**< 应用静态任务创建失败。 */
    APP_ERROR_SCHEDULER, /**< 调度器异常返回。 */
    APP_ERROR_MEASUREMENT, /**< ADC测量设备初始化失败。 */
    APP_ERROR_UART /**< TTL UART设备初始化失败。 */
} AppStatus;
/** @brief 保存应用启动的最近结果，供调试器观察。 */
extern volatile AppStatus g_app_status;
/** @brief 初始化各功能模块、创建静态运行对象并启动调度器。
 * @return 失败返回具体AppStatus；正常启动调度器不返回。 */
AppStatus App_MainStart(void);
#endif
