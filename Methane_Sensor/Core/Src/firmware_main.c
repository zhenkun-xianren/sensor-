#include "bsp_board.h"
#include "app_main.h"
#include "bsp_time.h"
#include "stm32f103xb.h"
#include "bsp_compiler.h"

static volatile BspBoardClockStatus s_clock_status; // 启动时钟配置状态，供 SWD 调试查看。

/**
 * @brief 配置板级时钟后启动 OLED 演示应用。
 * @return 正常调度时不会返回；启动失败停在故障状态。
 */
int main(void)
{
    s_clock_status = BSP_Board_ClockInit();
    if (s_clock_status != BSP_BOARD_CLOCK_OK || BSP_Time_Init() == 0U) {
        __disable_irq();
        for (;;) { }
    }

    if (App_Main_Run() == 0U) {
        __disable_irq();
        for (;;) { }
    }

    for (;;) { }
}
