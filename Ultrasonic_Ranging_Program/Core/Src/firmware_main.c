#include "bsp_startup.h"
#include "bsp_fault.h"
#include "bsp_backtrace.h"
#include "app_main.h"

/**
 * @brief 最先初始化无硬件依赖的诊断库，完成板级基础启动后进入APP。
 * @return 正常调度不返回；基础或应用启动失败时记录故障并永久停止。
 */
int main(void)
{
    BspStartupStatus board_status; // 板级基础启动的实际结果。
    int app_status; // APP 模块启动失败或调度器异常返回的原因。

    if (BSP_Backtrace_Init() != BSP_BACKTRACE_OK) {
        BSP_Fault_Stop(BSP_FAULT_BACKTRACE_INIT, __FILE__, (uint32_t)__LINE__);
    }
    board_status = BSP_Startup_Init();
    if (board_status != BSP_STARTUP_OK) {
        BSP_Fault_Stop((board_status == BSP_STARTUP_CLOCK_FAILED) ? BSP_FAULT_CLOCK : BSP_FAULT_BOARD_INIT,
                       __FILE__, (uint32_t)__LINE__);
    }
    app_status = AppMain_Start();
    BSP_Fault_Stop((app_status == APP_MAIN_SCHEDULER_RETURNED) ? BSP_FAULT_SCHEDULER : BSP_FAULT_APP_START,
                   __FILE__, (uint32_t)__LINE__);
    return app_status;
}
