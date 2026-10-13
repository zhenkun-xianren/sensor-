#include "app_main.h"
#include "app_display.h"
#include "app_ir_remote.h"
#include "app_key_control.h"
#include "app_modbus.h"
#include "app_runtime.h"
#include "app_actuator.h"
#include "app_ultrasonic_tx.h"
#include "app_ultrasonic_range.h"
#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief 编排既有模块及所选超声模式，再启动调度器；测距波形由任务启动。
 * @return 正常调度不返回；模块启动失败返回 APP_MAIN_MODULE_FAILED，
 *         调度器异常返回时返回 APP_MAIN_SCHEDULER_RETURNED。
 */
int AppMain_Start(void)
{
    if (AppActuator_Init() != DEV_OK) {
        return APP_MAIN_MODULE_FAILED;
    }
    if (AppDisplay_Start() != APP_DISPLAY_OK) {
        return APP_MAIN_MODULE_FAILED;
    }
    if (AppIrRemote_Start(AppKeyControl_HandleEvent, AppKeyControl_CanHandleEvent) != APP_IR_REMOTE_OK) {
        return APP_MAIN_MODULE_FAILED;
    }
    if (AppModbus_Start()) {
        return APP_MAIN_MODULE_FAILED;
    }
    if (AppRuntime_Start()) {
        return APP_MAIN_MODULE_FAILED;
    }
#if APP_MAIN_ENABLE_ULTRASONIC_RANGE_TEST
    if (APP_UltrasonicRange_Start() != DEV_OK) {
#else
    if (AppUltrasonicTx_Start() != DEV_OK) {
#endif
        return APP_MAIN_MODULE_FAILED;
    }
    vTaskStartScheduler();
    /* 硬件PWM不依赖调度器；异常返回必须显式关闭，不只停止CPU。 */
#if APP_MAIN_ENABLE_ULTRASONIC_RANGE_TEST
    (void)APP_UltrasonicRange_Stop();
#else
    (void)AppUltrasonicTx_Stop();
#endif
    return APP_MAIN_SCHEDULER_RETURNED;
}
