#include <stddef.h>
#include "port_example.h"
#include "bsp_example.h"

static DevExample s_example; // 本 Port 实现的唯一静态设备对象。

/**
 * @brief 检查设备对象并调用板级初始化，明确传递未实现状态。
 * @param self 必须为本 Port 的静态设备对象。
 * @return 无效对象返回 DEV_EXAMPLE_INVALID_ARGUMENT；当前有效调用返回
 * DEV_EXAMPLE_NOT_IMPLEMENTED；未来 BSP 成功才返回 DEV_EXAMPLE_OK。
 */
static DevExampleStatus example_init(DevExample *self)
{
    int result; // 保存 BSP 返回的实际初始化状态。
    if ((self == NULL) || (self != &s_example)) {
        return DEV_EXAMPLE_INVALID_ARGUMENT;
    }
    result = BSP_Example_Init();
    if (result == BSP_EXAMPLE_NOT_IMPLEMENTED) {
        return DEV_EXAMPLE_NOT_IMPLEMENTED;
    }
    return (result == 0) ? DEV_EXAMPLE_OK : DEV_EXAMPLE_INVALID_ARGUMENT;
}

/**
 * @brief 绑定示例设备方法并返回静态对象，不执行硬件初始化。
 * @return 本 Port 静态设备对象的非空指针；驱动可用性由 init 的返回值决定。
 */
DevExample *GetExample(void)
{
    s_example.init = example_init;
    return &s_example;
}
