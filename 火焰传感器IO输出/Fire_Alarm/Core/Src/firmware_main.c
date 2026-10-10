#include "stm32f0xx.h"
#include "bsp_board.h"
#include "app_main.h"

volatile BspBoardStatus g_firmware_board_status = BSP_BOARD_OK; // 调试器读取的板级启动结果。

/* 每个SysTick周期的CPU周期数作为停滞检查上限；不是精确毫秒超时。 */
enum { FIRMWARE_INIT_TICK_WAIT_LIMIT = BSP_BOARD_HCLK_HZ / BSP_BOARD_TICK_HZ };

/** @brief 启动阶段按真实毫秒时基等待，计时停滞时有界返回。
 * @param interval_ms 至少需要等待的毫秒数。
 * @return 完成等待返回true，毫秒时基持续不变时返回false。 */
static bool firmware_init_wait_ms(uint32_t interval_ms)
{
    uint32_t start_ms = BSP_Board_GetMs(); // 本次等待的实际起点。
    uint32_t observed_ms = start_ms; // 最近一次确认仍在推进的毫秒时刻。
    uint32_t stalled_checks = 0; // 两次毫秒变化之间的连续停滞检查次数。
    for (;;) {
        uint32_t now_ms = BSP_Board_GetMs(); // 实时读取，差值支持32位时基自然回绕。
        if ((uint32_t)(now_ms - start_ms) >= interval_ms) {
            return true;
        }
        if (now_ms != observed_ms) {
            observed_ms = now_ms;
            stalled_checks = 0;
        } else if (++stalled_checks >= FIRMWARE_INIT_TICK_WAIT_LIMIT) {
            return false; // 不依赖已停止的时基判断超时，也不进入无法有界退出的休眠。
        }
    }
}

static const AppFlameInitTiming s_flame_init_timing = {
    BSP_Board_GetMs, firmware_init_wait_ms
}; // 固件入口提供计时回调，APP保持与板级实现解耦。

/**
 * @brief 唯一正式入口，先启动板级基础，再串行调度火焰事件和主动上报。
 * @return 正常运行不返回；失败保存状态并停止后续执行。
 */
int main(void)
{
    g_firmware_board_status = BSP_Board_Init();
    if (g_firmware_board_status != BSP_BOARD_OK) {
        BSP_Board_Fault();
    }
    (void)App_Main_Init(BSP_Board_GetMs(), &s_flame_init_timing); /* 传感器启动重试全部完成后进入正常轮询。 */
    for (;;) {
        App_Main_Poll(BSP_Board_GetMs());
        __WFI(); /* SysTick/USART/TIM14唤醒，保留现有裸机架构，无忙等待。 */
    }
}
