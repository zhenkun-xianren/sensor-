#ifndef DEV_GPIO_H
#define DEV_GPIO_H
#include "dev_common.h"

/** @brief 仅配置本板执行器所属引脚的GPIO设备契约。 */
typedef struct DevGpio DevGpio;
/** @brief GPIO初始化能力，不包含业务控制。 */
struct DevGpio {
    /**
     * @brief 在启动阶段预置全部关闭电平并配置执行器引脚。
     * @param self GetGpio返回的设备对象。
     * @return 成功DEV_OK，对象错误DEV_ARGUMENT_ERROR，板级失败DEV_IO_ERROR。
     */
    DevStatus (*init)(DevGpio *self);
};
/**
 * @brief 获取静态GPIO设备接口。
 * @return 本工程唯一的GPIO初始化设备对象。
 */
DevGpio *GetGpio(void);
#endif
