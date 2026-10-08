#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H
#include <stdbool.h>
#include <stdint.h>
#define APP_SETTINGS_DEFAULT_MM 7000U // 首次上电默认7.0米。
#define APP_SETTINGS_STEP_MM 100U // 每次调整0.1米，Flash记录按同一步进校验。
#define APP_SETTINGS_MAX_MM 20000U // 最大20.0米。
#define APP_SETTINGS_MIN_MM 0U // 最小0.0米。
/** @brief 保存任务向显示状态机发布的阶段。 */
typedef enum {
 APP_SETTINGS_PREPARE, /**< 已独占总线，等待扫描停止确认。 */
 APP_SETTINGS_SAVED, /**< 数据写入并读回成功，可切换生效值。 */
 APP_SETTINGS_FAILED /**< 保存失败，旧生效值不变。 */
} AppSettingsEventType;
/** @brief 保存阶段消息。 */
typedef struct {
 AppSettingsEventType type; // 保存阶段。
 uint16_t threshold_mm; // 本次拟保存的设定值。
} AppSettingsEvent;
/** @brief 创建静态保存任务并读取开机值。 @return 资源创建成功返回true，Flash读取失败仍以默认值启动。 */
bool App_Settings_Start(void);
/** @brief 获取上电扫描后的初始值。 @return 有效设定值，单位毫米。 */
uint16_t App_Settings_InitialValue(void);
/** @brief 非阻塞请求保存。 @param threshold_mm 新设定值，单位毫米。 @return 请求入队成功返回true。 */
bool App_Settings_RequestSave(uint16_t threshold_mm);
/** @brief 非阻塞读取保存阶段。 @param event 输出消息。 @return 读取到消息返回true。 */
bool App_Settings_TakeEvent(AppSettingsEvent *event);
/** @brief 唯一扫描源停止输出后确认是否允许编程。 @param allowed 显示已安全关闭时为true。 */
void App_Settings_AllowSave(bool allowed);
/** @brief 独占完整RS485事务和Flash操作的公共互斥锁。 @return 在限定期限内获得锁返回true。 */
bool App_Settings_LockBus(void);
/** @brief 释放当前任务持有的总线互斥锁。 */
void App_Settings_UnlockBus(void);
#endif
