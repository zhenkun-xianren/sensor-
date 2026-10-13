#ifndef APP_EXAMPLE_H
#define APP_EXAMPLE_H
#include "dev_example.h"

/**
 * @brief 展示 APP 如何通过设备契约初始化；默认启动流程不调用此函数。
 * @return 对象或方法为空返回 DEV_EXAMPLE_INVALID_ARGUMENT；否则透传设备状态，
 * 当前为 DEV_EXAMPLE_NOT_IMPLEMENTED，不代表驱动完成。
 */
DevExampleStatus AppExample_Init(void);

#endif
