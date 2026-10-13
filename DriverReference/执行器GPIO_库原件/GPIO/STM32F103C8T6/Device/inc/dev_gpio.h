#ifndef DEV_GPIO_H
#define DEV_GPIO_H

#include "dev_common.h"

/**
 * @brief 提供板级 GPIO 初始化能力的设备接口。
 */
typedef struct DevGpio DevGpio;

struct DevGpio {
    /**
     * @brief 初始化本工程使用的板级 GPIO。
     * @param self 接收初始化请求的 GPIO 设备对象。
     * @return 初始化成功返回 DEV_OK，板级 GPIO 初始化失败返回 DEV_IO_ERROR。
     */
    DevStatus (*init)(DevGpio *self);
};

/**
 * @brief 获取 GPIO 设备接口。
 * @return 指向静态 GPIO 设备接口的指针。
 */
DevGpio *GetGpio(void);

#endif
