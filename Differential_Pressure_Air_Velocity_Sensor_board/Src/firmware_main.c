#include "bsp_board.h"
#include "bsp_backtrace.h"
#include "app_main.h"
/** @brief 唯一正式固件入口，完成板级基础启动后交给APP。
 * @return 正常运行不返回；所有启动失败进入可定位故障处理。 */
int main(void)
{
    if (BSP_Board_DebugInit() != BSP_BOARD_OK) BSP_Board_Fault(BSP_FAULT_DEBUG);
    if (BSP_Board_Init() != BSP_BOARD_OK) BSP_Board_Fault(BSP_FAULT_CLOCK);
    if (BSP_Backtrace_Init() != BSP_BOARD_OK) BSP_Board_Fault(BSP_FAULT_APP);
    if (App_MainStart() != APP_OK) BSP_Board_Fault(BSP_FAULT_APP);
    BSP_Board_Fault(BSP_FAULT_APP);
}
