#ifndef BSP_BOARD_H
#define BSP_BOARD_H
#include <stdint.h>
/* 内部HSI标称8MHz；RM0008 RCC章节，保留出厂校准并使用默认微调值。 */
#define BSP_BOARD_HSI_HZ 8000000UL
/* HSI直接提供系统时钟，AHB/APB1/APB2均不分频，HSE和PLL关闭。 */
#define BSP_BOARD_SYSCLK_HZ BSP_BOARD_HSI_HZ
#define BSP_BOARD_HCLK_HZ BSP_BOARD_SYSCLK_HZ
#define BSP_BOARD_PCLK1_HZ BSP_BOARD_HCLK_HZ
#define BSP_BOARD_PCLK2_HZ BSP_BOARD_HCLK_HZ
#define BSP_BOARD_TIM2_HZ BSP_BOARD_PCLK1_HZ
#define BSP_BOARD_NVIC_PRIORITY_BITS 4 /* RVDS汇编展开所需，无C整数后缀。 */
#define BSP_BOARD_PRIORITY_GROUP 0UL /* 全部实现位用于抢占优先级。 */
#define BSP_BOARD_TICK_HZ 1000UL
#define BSP_BOARD_WAIT_POLLS 100000UL
#define BSP_BOARD_HSI_DEFAULT_TRIM 16UL
#define BSP_BOARD_BITS_PER_PIN 4UL
#define BSP_BOARD_PIN_FIELD_MASK 15UL
#define BSP_BOARD_BANK_PIN_COUNT 8UL
#define BSP_BOARD_BSRR_RESET_SHIFT 16UL
#define BSP_BOARD_LOW 0U
#define BSP_BOARD_HIGH 1U
#define BSP_BOARD_OK 0
#define BSP_BOARD_ERROR (-1)
/* GPIO模式：RM0008 9.2.1/9.2.2，CNF[1:0]:MODE[1:0]。 */
#define BSP_BOARD_OUTPUT_PP_2MHZ 2UL
#define BSP_BOARD_OUTPUT_OD_2MHZ 6UL
#define BSP_BOARD_INPUT_FLOATING 4UL
#define BSP_BOARD_INPUT_PULL 8UL
#define BSP_BOARD_AF_PP_2MHZ 10UL
/** @brief 固件故障阶段，便于用户在调试器中定位。 */
typedef enum {
    BSP_FAULT_NONE = 0, /**< 未报告故障。 */
    BSP_FAULT_DEBUG, /**< 调试接口配置失败。 */
    BSP_FAULT_CLOCK, /**< 板级时钟或计时初始化失败。 */
    BSP_FAULT_APP, /**< 应用模块启动失败或调度器返回。 */
    BSP_FAULT_ASSERT, /**< FreeRTOS断言触发。 */
    BSP_FAULT_STACK, /**< FreeRTOS检测到栈溢出。 */
    BSP_FAULT_MEMORY, /**< 动态内存分配失败。 */
    BSP_FAULT_EXCEPTION /**< 处理器异常。 */
} BspBoardFault;
/** @brief 保存最近故障阶段，仅表示软件检测结果。 */
extern volatile BspBoardFault g_board_fault;
/** @brief 显式关闭JTAG并保留SWD；核对可读配置。
 * @return 成功BSP_BOARD_OK，失败BSP_BOARD_ERROR。 */
int BSP_Board_DebugInit(void);
/** @brief 建立内部HSI 8MHz系统时钟及1ms基础时基。
 * @return 成功BSP_BOARD_OK，失败BSP_BOARD_ERROR。 */
int BSP_Board_Init(void);
/** @brief 只读核对HSI就绪、HSE及PLL关闭、系统时钟与总线不分频配置。
 * @return 匹配BSP_BOARD_OK，否则BSP_BOARD_ERROR。 */
int BSP_Board_CheckClock(void);
/** @brief 读取独立于调度器启动状态的毫秒计数。
 * @return 允许自然回绕的毫秒计数。 */
uint32_t BSP_Time_GetMs(void);
/** @brief 尽力关闭已配置输出并停止后续执行。
 * @param reason 可定位的故障阶段。 */
void BSP_Board_Fault(BspBoardFault reason);
#endif
