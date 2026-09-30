#include "port_gpio.h"
#include "dev_gpio.h"
#include "bsp_gpio.h"

/**
 * @brief 初始化板级 GPIO，并将结果转换为设备状态。
 * @return GPIO 初始化成功时返回 DEV_OK，否则返回 DEV_IO_ERROR。
 */
DevStatus PortGpio_Init(void)
{
    return BSP_Gpio_Init() == 0 ? DEV_OK : DEV_IO_ERROR;
}

/**
 * @brief 通过 Port 接口初始化 GPIO 设备。
 * @param self 接收初始化请求的 GPIO 设备对象。
 * @return 初始化成功时返回 DEV_OK，否则返回 DEV_IO_ERROR。
 */
static DevStatus gpio_init(DevGpio *self)
{
    if (self == 0) {
        return DEV_IO_ERROR;
    }
    return PortGpio_Init();
}

static DevGpio s_gpio = {
    .init = gpio_init
};

/**
 * @brief 返回静态分配的 GPIO 设备接口。
 * @return 指向 GPIO 设备接口的指针。
 */
DevGpio *GetGpio(void)
{
    return &s_gpio;
}
