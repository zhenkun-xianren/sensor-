#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include <stdint.h>

/* X2=YC8MLBCD2XT，8MHz无源晶振；HSE直连SYSCLK，AHB/APB不分频。 */
#define BSP_BOARD_HSE_HZ          8000000UL // 板载X2标称频率，单位Hz。
#define BSP_BOARD_SYSCLK_HZ       BSP_BOARD_HSE_HZ
#define BSP_BOARD_HCLK_HZ         BSP_BOARD_SYSCLK_HZ
#define BSP_BOARD_PCLK1_HZ        BSP_BOARD_HCLK_HZ
#define BSP_BOARD_PCLK2_HZ        BSP_BOARD_HCLK_HZ
#define BSP_BOARD_TIM14_HZ        BSP_BOARD_PCLK1_HZ
#define BSP_BOARD_TICK_HZ         1000UL
#define BSP_BOARD_WAIT_LIMIT      800000UL // 时钟启动各阶段最多轮询次数，不作为毫秒时基。
#define BSP_BOARD_SYSTICK_PRIORITY 2U
#define BSP_BOARD_FIELD_CLEAR     0UL
#define BSP_BOARD_SWD_AF          0UL
#define BSP_BOARD_CLOCK_HSI       0UL
#define BSP_BOARD_FLASH_LATENCY   0UL

/** @brief 板级启动结果。 */
typedef enum {
    BSP_BOARD_OK = 0,          /**< 时钟、计时和 SWD 初始化完成。 */
    BSP_BOARD_DEBUG_ERROR = 1, /**< SWD 引脚读回不匹配。 */
    BSP_BOARD_CLOCK_ERROR = 2, /**< 时钟就绪超时或频率不匹配。 */
    BSP_BOARD_TICK_ERROR = 3   /**< SysTick 参数无效。 */
} BspBoardStatus;

/** @brief 先显式保留 SWD，再初始化 HSE 8 MHz 时钟和毫秒计时。
 * @return 板级启动结果，不访问传感器和 RS485 引脚。 */
BspBoardStatus BSP_Board_Init(void);
/** @brief 获取允许自然回绕的毫秒时间。
 * @return 自启动以来的毫秒计数。 */
uint32_t BSP_Board_GetMs(void);
/** @brief 停止后续执行，供调试器读取入口保存的故障状态。 */
void BSP_Board_Fault(void);

#endif
