#include "port_gpio.h"
#include "bsp_gpio.h"
static DevGpio s_gpio; // 本工程唯一的GPIO设备接口对象。

/**
 * @brief 初始化所选BSP拥有的执行器GPIO，并转换板级返回状态。
 * @return 成功DEV_OK，板级初始化失败DEV_IO_ERROR。
 */
DevStatus PortGpio_Init(void)
{
    return BSP_Gpio_Init() == 0 ? DEV_OK : DEV_IO_ERROR;
}
/**
 * @brief 检查设备身份并初始化执行器所属GPIO。
 * @param self GetGpio返回的对象。
 * @return 成功DEV_OK，对象无效DEV_ARGUMENT_ERROR，板级失败DEV_IO_ERROR。
 */
static DevStatus gpio_init(DevGpio *self)
{
    if (self != &s_gpio) return DEV_ARGUMENT_ERROR;
    return PortGpio_Init();
}
/**
 * @brief 绑定GPIO设备初始化接口。
 * @return 静态GPIO设备对象，不重置其硬件状态。
 */
DevGpio *GetGpio(void)
{
    s_gpio.init = gpio_init;
    return &s_gpio;
}
