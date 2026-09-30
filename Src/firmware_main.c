#include "bsp_board.h"
#include "app_main.h"
#include "bsp_gpio.h"
#include "bsp_time.h"
#include "stm32f103xb.h"
#include "bsp_compiler.h"

static volatile BspBoardClockStatus s_clock_status; // 启动时钟配置状态，供 SWD 调试查看。

/**
 * @brief 完成手写板级初始化，随后启动 FreeRTOS 应用。
 * @return 正常调度时不会返回；启动失败停在故障状态。
 */
int main(void)
{
    /* 先点亮两灯，再切换主频；时钟失败时仍能观察到故障灯态。 */
    BSP_Gpio_InitIndicators();
    if (BSP_Gpio_IndicatorsAreOn() == 0U) {
        BSP_Gpio_IndicateFault();
        __disable_irq();
        for (;;) { }
    }

    BSP_Gpio_InitSafeOutputs();
    if (BSP_Gpio_SafeOutputsAreOff() == 0U) {
        BSP_Gpio_IndicateFault();
        __disable_irq();
        for (;;) { }
    }

    s_clock_status = BSP_Board_ClockInit();
    if (s_clock_status != BSP_BOARD_CLOCK_OK || BSP_Time_Init() == 0U) {
        BSP_Gpio_IndicateFault();
        __disable_irq();
        for (;;) { }
    }

    if (App_Main_Run() == 0U) {
        BSP_Gpio_IndicateFault();
        __disable_irq();
        for (;;) { }
    }

    for (;;) { }
}
