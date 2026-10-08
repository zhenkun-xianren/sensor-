#include "bsp_board.h"
#include "app_main.h"
#include "stm32f103xb.h"

static volatile BspBoardResult s_board_result; // 保存板级启动结果以便定位失败阶段。
static volatile AppMainResult s_app_result = APP_MAIN_NOT_STARTED; // 保存应用模块或调度器启动失败结果。

/** @brief 唯一正式固件入口，先建立板级资源再启动应用。 @return 正常调度不返回。 */
int main(void)
{
    s_board_result = BSP_Board_Init();
    if (s_board_result == BSP_BOARD_OK) s_app_result = App_Main_Run();
    /* 初始化失败或调度器异常返回均停在可查看结果的位置。 */
    __disable_irq();
    for (;;) { }
}

/** @brief 为保留的厂商库提供唯一错误入口，不继续执行依赖失败资源的操作。 */
void Error_Handler(void)
{
    __disable_irq();
    for (;;) { }
}
