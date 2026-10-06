#ifndef DEV_ACTUATOR_H
#define DEV_ACTUATOR_H
#include <stdint.h>
#include "dev_common.h"

/** @brief 逻辑驱动状态；每项0关闭、1开启，不代表触点或实际负载反馈。 */
typedef struct {
    uint8_t relay; /**< 继电器线圈驱动：0释放、1吸合请求。 */ // 线圈逻辑驱动状态。
    uint8_t optocoupler; /**< 现场输出：0拉低、1高电平请求。 */ // 现场输出逻辑状态。
    uint8_t buzzer; /**< 蜂鸣器：0关闭、1开启。 */ // 蜂鸣器开关状态。
    uint8_t warningLed; /**< 报警灯：0熄灭、1点亮。 */ // 报警灯驱动状态。
    uint8_t powerLed; /**< 电源灯：0熄灭、1点亮。 */ // 电源灯驱动状态。
} DevActuatorState;
/** @brief 本板GPIO开关执行器设备对象。 */
typedef struct DevActuator DevActuator;
/** @brief 统一继电器、光耦、蜂鸣器和两只独立指示灯的能力。 */
struct DevActuator {
    /**
     * @brief GPIO初始化成功后建立全部关闭状态，两灯均熄灭。
     * @param self GetActuator返回的唯一设备对象。
     * @return 成功DEV_OK，对象错误DEV_ARGUMENT_ERROR，GPIO未就绪DEV_NOT_READY，板级失败DEV_IO_ERROR。
     */
    DevStatus (*init)(DevActuator *self);
    /**
     * @brief 请求继电器吸合或释放。
     * @param self 执行器设备对象。
     * @param enabled 零释放、非零吸合。
     * @return 成功DEV_OK，对象错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY，板级失败DEV_IO_ERROR。
     */
    DevStatus (*setRelay)(DevActuator *self, uint8_t enabled);
    /**
     * @brief 请求现场光耦输出电平，内部GPIO电平反相。
     * @param self 执行器设备对象。
     * @param enabled 零使PC817导通并拉低现场输出；非零使现场输出为高。
     * @return 成功DEV_OK，对象错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY，板级失败DEV_IO_ERROR。
     */
    DevStatus (*setOptocoupler)(DevActuator *self, uint8_t enabled);
    /**
     * @brief 开关有源蜂鸣器。
     * @param self 执行器设备对象。
     * @param enabled 零关闭、非零开启，不产生PWM。
     * @return 成功DEV_OK，对象错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY，板级失败DEV_IO_ERROR。
     */
    DevStatus (*setBuzzer)(DevActuator *self, uint8_t enabled);
    /**
     * @brief 请求成对更新两灯，具体端口和原子写入能力由BSP决定。
     * @param self 执行器设备对象。
     * @param warning_enabled 零熄灭、非零点亮报警灯。
     * @param power_enabled 零熄灭、非零点亮电源灯。
     * @return 成功DEV_OK，对象错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY，板级失败DEV_IO_ERROR。
     */
    DevStatus (*setIndicatorLeds)(DevActuator *self, uint8_t warning_enabled, uint8_t power_enabled);
    /**
     * @brief 查询输出锁存对应的逻辑状态，失败不修改输出对象。
     * @param self 执行器设备对象。
     * @param output 非空状态输出指针；本接口不采集机械触点或实际负载。
     * @return 成功DEV_OK，参数错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY，板级失败DEV_IO_ERROR。
     */
    DevStatus (*getState)(DevActuator *self, DevActuatorState *output);
};
/**
 * @brief 获取静态执行器接口。
 * @return 本工程唯一的执行器设备对象。
 */
DevActuator *GetActuator(void);
#endif
