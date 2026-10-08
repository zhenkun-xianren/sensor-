#include "app_main.h"
#include "app_key.h"
#include "app_display.h"
#include "app_rs485.h"
#include "app_settings.h"
#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief 恢复Flash设定并启动显示、按键、雷达查询模块及唯一调度器。
 * @return 模块失败或调度器异常返回的明确结果。
 */
AppMainResult App_Main_Run(void)
{
    if (!App_Settings_Start()) return APP_MAIN_SETTINGS_ERROR;
    if (App_Display_Start() != APP_DISPLAY_OK) return APP_MAIN_DISPLAY_ERROR;
    if (App_Key_Start() != APP_KEY_OK) return APP_MAIN_KEY_ERROR;
    if (App_Rs485_Start() != APP_RS485_OK) return APP_MAIN_RS485_ERROR;
    vTaskStartScheduler();
    return APP_MAIN_SCHEDULER_ERROR;
}
