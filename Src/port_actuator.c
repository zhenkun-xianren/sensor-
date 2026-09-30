#include "port_actuator.h"
#include "bsp_actuator.h"

static DevActuator s_actuator; // 绑定执行器 Port 实现的静态设备接口对象。

/**
 * @brief 初始化继电器、光耦隔离输出和蜂鸣器设备。
 * @param self 执行器设备对象指针。
 * @return 安全初值和两灯正确返回 1，否则返回 0。
 */
static uint8_t actuator_init(DevActuator *self)
{
    (void)self;
    return BSP_Actuator_Init();
}

/**
 * @brief 按指定状态控制继电器驱动输出。
 * @param self 执行器设备对象指针。
 * @param enabled 非零时吸合继电器，零时释放继电器。
 * @return 输出状态正确返回 1，否则返回 0。
 */
static uint8_t actuator_set_relay(DevActuator *self, uint8_t enabled)
{
    (void)self;
    return BSP_Actuator_SetRelay(enabled);
}

/**
 * @brief 按指定状态控制光耦隔离输出。
 * @param self 执行器设备对象指针。
 * @param enabled 非零时开启光耦输出，零时关闭光耦输出。
 * @return 输出状态正确返回 1，否则返回 0。
 */
static uint8_t actuator_set_optocoupler(DevActuator *self, uint8_t enabled)
{
    (void)self;
    return BSP_Actuator_SetOptocoupler(enabled);
}

/**
 * @brief 按指定状态控制蜂鸣器输出。
 * @param self 执行器设备对象指针。
 * @param enabled 非零时开启蜂鸣器，零时关闭蜂鸣器。
 * @return 输出状态正确返回 1，否则返回 0。
 */
static uint8_t actuator_set_buzzer(DevActuator *self, uint8_t enabled)
{
    (void)self;
    return BSP_Actuator_SetBuzzer(enabled);
}

/**
 * @brief 通过一次板级 GPIO 操作同时设置两路指示灯。
 * @param self 执行器设备对象指针。
 * @param warning_enabled 非零时点亮 WARNING 指示灯，零时熄灭。
 * @param power_enabled 非零时点亮 POWER 指示灯，零时熄灭。
 * @return 两路输出状态正确返回 1，否则返回 0。
 */
static uint8_t actuator_set_indicator_leds(DevActuator *self,
                                           uint8_t warning_enabled,
                                           uint8_t power_enabled)
{
    (void)self;
    return BSP_Actuator_SetIndicatorLeds(warning_enabled, power_enabled);
}

/**
 * @brief 获取执行器设备接口并绑定 Port 实现。
 * @return 指向静态执行器设备接口的指针。
 */
DevActuator *GetActuator(void)
{
    s_actuator.init = actuator_init;
    s_actuator.setRelay = actuator_set_relay;
    s_actuator.setOptocoupler = actuator_set_optocoupler;
    s_actuator.setBuzzer = actuator_set_buzzer;
    s_actuator.setIndicatorLeds = actuator_set_indicator_leds;
    return &s_actuator;
}
