#include "port_actuator.h"
#include "bsp_actuator.h"
#include <stddef.h>

static DevActuator s_actuator; // 本工程唯一执行器接口对象。
static uint8_t s_initialized; // 标记执行器初始化是否成功。

/**
 * @brief 核对设备身份及初始化状态。
 * @param self 调用方提供的执行器对象。
 * @return 对象无效DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY，否则DEV_OK。
 */
static DevStatus actuator_check(DevActuator *self)
{
    if (self != &s_actuator) return DEV_ARGUMENT_ERROR;
    return s_initialized != 0U ? DEV_OK : DEV_NOT_READY;
}
/**
 * @brief 将零关闭、非零开启转换成板级GPIO锁存电平。
 * @param enabled 逻辑开关参数。
 * @param active 对应通道开启所需的GPIO电平。
 * @return 0或1的GPIO锁存电平。
 */
static uint8_t actuator_level(uint8_t enabled, uint8_t active)
{
    return enabled != 0U ? active : (uint8_t)(1U - active);
}
/**
 * @brief 初始化执行器，要求GPIO已初始化，默认全部关闭。
 * @param self 执行器对象。
 * @return 成功DEV_OK，对象错误DEV_ARGUMENT_ERROR，GPIO未初始化DEV_NOT_READY，板级失败DEV_IO_ERROR。
 */
static DevStatus actuator_init(DevActuator *self)
{
    if (self != &s_actuator) return DEV_ARGUMENT_ERROR;
    s_initialized = 0U;
    if (BSP_Gpio_IsInitialized() == 0) return DEV_NOT_READY;
    if (BSP_Actuator_Init() != 0) return DEV_IO_ERROR;
    s_initialized = 1U;
    return DEV_OK;
}
/**
 * @brief 检查设备并控制单个逻辑通道。
 * @param self 执行器对象。
 * @param channel 板级通道编号。
 * @param enabled 零关闭、非零开启。
 * @param active 板级开启电平。
 * @return 成功DEV_OK，参数错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY，板级失败DEV_IO_ERROR。
 */
static DevStatus actuator_set(DevActuator *self, BspActuatorChannel channel, uint8_t enabled, uint8_t active)
{
    DevStatus status = actuator_check(self); // 保存对象和初始化检查结果。
    if (status != DEV_OK) return status;
    return BSP_Actuator_Set(channel, actuator_level(enabled, active)) == 0 ? DEV_OK : DEV_IO_ERROR;
}
#if DEV_ACTUATOR_ENABLE_RELAY
/**
 * @brief 控制继电器线圈驱动。
 * @param self 执行器对象。
 * @param enabled 零释放、非零吸合。
 * @return 返回统一执行器状态码。
 */
static DevStatus actuator_set_relay(DevActuator *self, uint8_t enabled)
{
    return actuator_set(self, BSP_ACTUATOR_RELAY, enabled, BSP_GPIO_RELAY_ACTIVE);
}
#endif /* DEV_ACTUATOR_ENABLE_RELAY */
#if DEV_ACTUATOR_ENABLE_OPTOCOUPLER
/**
 * @brief 控制现场光耦输出，GPIO与现场电平反相。
 * @param self 执行器对象。
 * @param enabled 零表示现场拉低、非零表示现场高电平。
 * @return 返回统一执行器状态码。
 */
static DevStatus actuator_set_optocoupler(DevActuator *self, uint8_t enabled)
{
    return actuator_set(self, BSP_ACTUATOR_OPTOCOUPLER, enabled, BSP_GPIO_OPTO_ACTIVE);
}
#endif /* DEV_ACTUATOR_ENABLE_OPTOCOUPLER */
#if DEV_ACTUATOR_ENABLE_BUZZER
/**
 * @brief 控制有源蜂鸣器。
 * @param self 执行器对象。
 * @param enabled 零关闭、非零开启。
 * @return 返回统一执行器状态码。
 */
static DevStatus actuator_set_buzzer(DevActuator *self, uint8_t enabled)
{
    return actuator_set(self, BSP_ACTUATOR_BUZZER, enabled, BSP_GPIO_BUZZER_ACTIVE);
}
#endif /* DEV_ACTUATOR_ENABLE_BUZZER */
#if DEV_ACTUATOR_ENABLE_INDICATOR_LEDS
/**
 * @brief 将两灯的逻辑开关转换为板级电平，并交由BSP成对更新。
 * @param self 执行器对象。
 * @param warning_enabled 零熄灭、非零点亮报警灯。
 * @param power_enabled 零熄灭、非零点亮电源灯。
 * @return 返回统一执行器状态码。
 */
static DevStatus actuator_set_indicator_leds(DevActuator *self, uint8_t warning_enabled, uint8_t power_enabled)
{
    DevStatus status = actuator_check(self); // 保存设备可用性结果。
    if (status != DEV_OK) return status;
    return BSP_Actuator_SetIndicatorLevels(actuator_level(warning_enabled, BSP_GPIO_WARNING_ACTIVE),
        actuator_level(power_enabled, BSP_GPIO_POWER_ACTIVE)) == 0 ? DEV_OK : DEV_IO_ERROR;
}
#endif /* DEV_ACTUATOR_ENABLE_INDICATOR_LEDS */
#if DEV_ACTUATOR_ENABLE_STATE
/**
 * @brief 转换输出锁存快照为逻辑状态，仅成功时修改输出对象。
 * @param self 执行器对象。
 * @param output 接收五路逻辑状态的非空对象。
 * @return 返回统一执行器状态码。
 */
static DevStatus actuator_get(DevActuator *self, DevActuatorState *output)
{
    DevStatus status; // 保存设备检查结果。
    BspActuatorLevels levels; // 保存BSP返回的原始GPIO锁存快照。
    DevActuatorState state; // 保存本次完整逻辑状态。
    if (output == NULL) return DEV_ARGUMENT_ERROR;
    status = actuator_check(self);
    if (status != DEV_OK) return status;
    if (BSP_Actuator_Get(&levels) != 0) return DEV_IO_ERROR;
    state.relay = levels.relay == BSP_GPIO_RELAY_ACTIVE ? 1U : 0U;
    state.optocoupler = levels.optocoupler == BSP_GPIO_OPTO_ACTIVE ? 1U : 0U;
    state.buzzer = levels.buzzer == BSP_GPIO_BUZZER_ACTIVE ? 1U : 0U;
    state.warningLed = levels.warningLed == BSP_GPIO_WARNING_ACTIVE ? 1U : 0U;
    state.powerLed = levels.powerLed == BSP_GPIO_POWER_ACTIVE ? 1U : 0U;
    *output = state;
    return DEV_OK;
}
#endif /* DEV_ACTUATOR_ENABLE_STATE */
/**
 * @brief 绑定全部执行器方法，获取对象不改变输出或初始化状态。
 * @return 本工程静态执行器设备对象。
 */
DevActuator *GetActuator(void)
{
    s_actuator.init = actuator_init;
#if DEV_ACTUATOR_ENABLE_RELAY
    s_actuator.setRelay = actuator_set_relay;
#endif
#if DEV_ACTUATOR_ENABLE_OPTOCOUPLER
    s_actuator.setOptocoupler = actuator_set_optocoupler;
#endif
#if DEV_ACTUATOR_ENABLE_BUZZER
    s_actuator.setBuzzer = actuator_set_buzzer;
#endif
#if DEV_ACTUATOR_ENABLE_INDICATOR_LEDS
    s_actuator.setIndicatorLeds = actuator_set_indicator_leds;
#endif
#if DEV_ACTUATOR_ENABLE_STATE
    s_actuator.getState = actuator_get;
#endif
    return &s_actuator;
}
