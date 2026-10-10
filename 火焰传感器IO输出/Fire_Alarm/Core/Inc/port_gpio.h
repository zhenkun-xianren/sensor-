#ifndef PORT_GPIO_H
#define PORT_GPIO_H
#include "dev_gpio.h"

/**
 * @brief 初始化所选BSP拥有的执行器GPIO，并转换板级返回状态。
 * @return 成功DEV_OK，板级初始化失败DEV_IO_ERROR；APP应通过DevGpio调用。
 */
DevStatus PortGpio_Init(void);
#endif
