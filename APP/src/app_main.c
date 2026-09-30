#include "app_main.h"
#include "app_buzzer.h"
#include "app_display.h"
#include "app_ir_relay.h"
#include "app_system.h"
#include "dev_modbus.h"
#include "FreeRTOS.h"
#include "task.h"

volatile DevModbusStatus g_app_modbus_init_status; // Modbus/RS485 初始化结果，仅供调试观察；失败不阻止主应用启动。

/**
 * @brief 启动已注册的 APP 模块并运行 FreeRTOS 调度器。
 * @return 启动失败或调度器意外返回时返回 0；正常调度不会返回。
 */
uint8_t App_Main_Run(void)
{
    DevModbus *modbus; // 提供 Modbus 设备初始化接口的静态对象。

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

    /* 通讯外设失败时保留诊断状态，但继续启动阶段 06 的任务和蜂鸣器演示。 */
    modbus = GetModbus();
    if (modbus == 0 || modbus->init == 0) {
        g_app_modbus_init_status = DEV_MODBUS_ERROR_ARGUMENT;
    } else {
        g_app_modbus_init_status = modbus->init(modbus);
    }

    if (AppBuzzer_StartBootDemo() == 0U) {
        return 0U;
    }

    vTaskStartScheduler();
    return 0U;
}
