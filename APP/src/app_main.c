#include "app_main.h"
#include "app_actuator.h"
#include "app_display.h"
#include "app_ir.h"
#include "app_modbus.h"
#include "app_measurement.h"
#include "app_uart_echo.h"
#include "dev_gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>
volatile AppStatus g_app_status = APP_OK; // 保存最近启动结果。
/** @brief 编排现有设备及 UART 回传模块启动；具体任务和定时器由各功能模块创建。
 * @return 失败返回具体阶段，正常调度不返回。 */
AppStatus App_MainStart(void)
{
    DevGpio *gpio = GetGpio(); // 保存执行器GPIO初始化Device对象。
    if (gpio == NULL || gpio->init(gpio) != DEV_OK) return g_app_status = APP_ERROR_GPIO;
    g_app_status = App_Actuator_Init(); if (g_app_status != APP_OK) return g_app_status;
    g_app_status = App_Display_Init(); if (g_app_status != APP_OK) return g_app_status;
    g_app_status = App_Measurement_Init(); if (g_app_status != APP_OK) return g_app_status;
    g_app_status = App_Modbus_Init(); if (g_app_status != APP_OK) return g_app_status;
    g_app_status = App_Ir_Init(); if (g_app_status != APP_OK) return g_app_status;
    g_app_status = App_UartEcho_Init(); if (g_app_status != APP_OK) return g_app_status;
    g_app_status = App_Display_Start(); if (g_app_status != APP_OK) return g_app_status;
    g_app_status = App_Measurement_Start(); if (g_app_status != APP_OK) return g_app_status;
    g_app_status = App_Ir_Start(); if (g_app_status != APP_OK) return g_app_status;
    g_app_status = App_Modbus_Start(); if (g_app_status != APP_OK) return g_app_status;
    g_app_status = App_UartEcho_Start(); if (g_app_status != APP_OK) return g_app_status;
    g_app_status = App_Actuator_Start(); if (g_app_status != APP_OK) return g_app_status;
    vTaskStartScheduler();
    return g_app_status = APP_ERROR_SCHEDULER;
}
