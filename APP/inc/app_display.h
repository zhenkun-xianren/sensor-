#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H
#include "app_main.h"
#include "dev_display.h"
/** @brief 保存最近显示扫描结果，供调试器观察。 */
extern volatile DevDisplayStatus g_display_scan_status;
/** @brief 保存最近电压或错误帧的提交结果。 */
extern volatile DevDisplayStatus g_display_content_status;
/** @brief 获取Device并初始化开机等待测量的E01显示帧。
 * @return 成功APP_OK，否则APP_ERROR_DISPLAY。 */
AppStatus App_Display_Init(void);
/** @brief 创建并启动1ms静态扫描软件定时器。
 * @return 成功APP_OK，否则APP_ERROR_TIMER。 */
AppStatus App_Display_Start(void);
#endif
