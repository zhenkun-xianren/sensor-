#include <stddef.h>
#include "app_example.h"

/**
 * @brief 获取设备并检查句柄及方法，再透传初始化结果。
 * @return 对象或方法无效返回 DEV_EXAMPLE_INVALID_ARGUMENT，否则返回设备实际状态。
 */
DevExampleStatus AppExample_Init(void)
{
    DevExample *device = GetExample(); // 本次初始化使用的设备接口对象。
    DevExampleStatus result; // 保存初始化实际结果，供上层判断是否可用。
    if ((device == NULL) || (device->init == NULL)) {
        return DEV_EXAMPLE_INVALID_ARGUMENT;
    }
    result = device->init(device);
    if (result != DEV_EXAMPLE_OK) {
        return result;
    }
    return DEV_EXAMPLE_OK;
}
