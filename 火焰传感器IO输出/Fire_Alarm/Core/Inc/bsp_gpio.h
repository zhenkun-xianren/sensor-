#ifndef BSP_GPIO_H
#define BSP_GPIO_H
#include <stdint.h>
#include "dev_actuator_config.h"
/* 正式引脚表PA8→R7.2。PC817输入与现场输出反相：低使现场高，高使现场低。 */
#define BSP_GPIO_OPTO_PIN       8U
#define BSP_GPIO_OPTO_ACTIVE    BSP_GPIO_LEVEL_LOW
#define BSP_GPIO_OPTO_OFF       BSP_GPIO_LEVEL_HIGH
/** @brief 原始GPIO电平，与现场逻辑不同。 */
typedef enum { BSP_GPIO_LEVEL_LOW = 0, BSP_GPIO_LEVEL_HIGH = 1 } BspGpioLevel;
/** @brief 保持库的整数返回契约。 */
typedef enum { BSP_GPIO_OK = 0, BSP_GPIO_ERROR = -1 } BspGpioResult;
/** @brief 保持库的初始化查询契约。 */
typedef enum { BSP_GPIO_NOT_INITIALIZED = 0, BSP_GPIO_INITIALIZED = 1 } BspGpioState;
/** @brief 仅初始化光耦PA8，先置高，再配置推挽、低速、无上下拉。
 * @return 配置及默认现场低电平锁存符合返回OK，否则ERROR。 */
int BSP_Gpio_Init(void);
/** @brief 查询最近一次初始化结果。
 * @return 已初始化返回INITIALIZED，否则NOT_INITIALIZED。 */
int BSP_Gpio_IsInitialized(void);
/** @brief 在调用者已屏蔽中断时核对PA8配置和时钟。
 * @return 正常返回OK，否则ERROR。 */
int BSP_Gpio_CheckLocked(void);
/** @brief 在调用者已屏蔽中断时尽力置PA8高，使现场输出拉低。 */
void BSP_Gpio_OffLocked(void);
#endif
