#include "app_main.h"
#include "app_buzzer.h"
#include "app_display.h"
#include "app_ir_relay.h"
#include "app_system.h"
#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief 启动已注册的 APP 模块并运行 FreeRTOS 调度器。
 * @return 启动失败或调度器意外返回时返回 0；正常调度不会返回。
 */
uint8_t App_Main_Run(void)
{
    if (AppBuzzer_Init() == 0U) {
        return 0U;
    }
    if (AppDisplay_Init() == 0U) {
        return 0U;
    }
    if (AppIrRelay_Init() == 0U) {
        return 0U;
    }
    if (App_System_Start() == 0U) {
        return 0U;
    }
    if (AppBuzzer_StartBootDemo() == 0U) {
        return 0U;
    }

    vTaskStartScheduler();
    return 0U;
}
