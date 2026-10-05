#include "app_main.h"
#include "app_display.h"
#include "app_ir_remote.h"
#include "app_rs485.h"
#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief 依次创建显示、红外和 RS485 模块，再运行 FreeRTOS 调度器。
 * @return 任务创建失败或调度器意外返回时返回 0；正常调度不会返回。
 */
uint8_t App_Main_Run(void)
{
    if (AppDisplay_Start() == 0U || AppIrRemote_Start() == 0U) {
        return 0U;
    }
    if (AppRs485_Start() == 0U) {
        return 0U;
    }
    vTaskStartScheduler();
    return 0U;
}
