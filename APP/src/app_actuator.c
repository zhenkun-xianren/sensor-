#include "app_actuator.h"
#include "dev_gpio.h"
#include <stddef.h>

static DevActuator *s_device; // 缓存经接口检查且初始化成功的执行器对象。

/**
 * @brief 尽力关闭全部执行器；失败不证明实际负载已关闭。
 * @param device 已检查完整方法表的执行器对象。
 */
static void actuator_try_off(DevActuator *device)
{
    (void)device->setRelay(device, 0U);
    (void)device->setOptocoupler(device, 0U);
    (void)device->setBuzzer(device, 0U);
    (void)device->setIndicatorLeds(device, 0U, 0U);
}
/**
 * @brief 控制失败时尽力关闭全部执行器，保留原始错误码。
 * @param status 原始操作返回结果。
 * @return 原始结果，不被关闭操作的结果覆盖。
 */
static DevStatus actuator_finish(DevStatus status)
{
    if (status != DEV_OK && s_device != NULL) actuator_try_off(s_device);
    return status;
}
/**
 * @brief 检查GPIO及执行器契约，初始化全部关闭后点亮电源灯。
 * @return 成功DEV_OK，方法缺失DEV_ARGUMENT_ERROR，未就绪DEV_NOT_READY，板级失败DEV_IO_ERROR。
 */
DevStatus AppActuator_Init(void)
{
    DevGpio *gpio = GetGpio(); // 获取执行器引脚初始化契约。
    DevActuator *device = GetActuator(); // 获取执行器控制契约。
    DevStatus status; // 保存逐步初始化结果。
    s_device = NULL;
    if (gpio == NULL || gpio->init == NULL || device == NULL || device->init == NULL ||
        device->setRelay == NULL || device->setOptocoupler == NULL || device->setBuzzer == NULL ||
        device->setIndicatorLeds == NULL || device->getState == NULL) return DEV_ARGUMENT_ERROR;
    status = gpio->init(gpio);
    if (status != DEV_OK) return status;
    status = device->init(device);
    if (status != DEV_OK) {
        actuator_try_off(device);
        return status;
    }
    status = device->setIndicatorLeds(device, 0U, 1U);
    if (status != DEV_OK) {
        actuator_try_off(device);
        return status;
    }
    s_device = device;
    return DEV_OK;
}
/**
 * @brief 请求继电器线圈吸合或释放。
 * @param enabled 零释放、非零吸合。
 * @return 未初始化DEV_NOT_READY；否则返回设备原始结果，失败尽力全部关闭。
 */
DevStatus AppActuator_SetRelay(uint8_t enabled)
{
    if (s_device == NULL) return DEV_NOT_READY;
    return actuator_finish(s_device->setRelay(s_device, enabled));
}
/**
 * @brief 请求现场光耦输出电平。
 * @param enabled 零拉低现场输出、非零使现场输出为高。
 * @return 未初始化DEV_NOT_READY；否则返回设备原始结果，失败尽力全部关闭。
 */
DevStatus AppActuator_SetOptocoupler(uint8_t enabled)
{
    if (s_device == NULL) return DEV_NOT_READY;
    return actuator_finish(s_device->setOptocoupler(s_device, enabled));
}
/**
 * @brief 开关有源蜂鸣器。
 * @param enabled 零关闭、非零开启。
 * @return 未初始化DEV_NOT_READY；否则返回设备原始结果，失败尽力全部关闭。
 */
DevStatus AppActuator_SetBuzzer(uint8_t enabled)
{
    if (s_device == NULL) return DEV_NOT_READY;
    return actuator_finish(s_device->setBuzzer(s_device, enabled));
}
/**
 * @brief 请求报警灯及电源灯的组合状态。
 * @param warning_enabled 零熄灭、非零点亮报警灯。
 * @param power_enabled 零熄灭、非零点亮电源灯。
 * @return 未初始化DEV_NOT_READY；否则返回设备原始结果，失败尽力全部关闭。
 */
DevStatus AppActuator_SetIndicatorLeds(uint8_t warning_enabled, uint8_t power_enabled)
{
    if (s_device == NULL) return DEV_NOT_READY;
    return actuator_finish(s_device->setIndicatorLeds(s_device, warning_enabled, power_enabled));
}
/**
 * @brief 查询五路驱动状态；失败不修改输出对象，不代表负载反馈。
 * @param output 非空逻辑状态输出对象。
 * @return 参数错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY；否则返回设备查询状态。
 */
DevStatus AppActuator_GetState(DevActuatorState *output)
{
    if (output == NULL) return DEV_ARGUMENT_ERROR;
    if (s_device == NULL) return DEV_NOT_READY;
    return s_device->getState(s_device, output);
}
