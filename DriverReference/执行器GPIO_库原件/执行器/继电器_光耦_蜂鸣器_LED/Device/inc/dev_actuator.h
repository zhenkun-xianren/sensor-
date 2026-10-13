#ifndef DEV_ACTUATOR_H
#define DEV_ACTUATOR_H

#include <stdint.h>

/**
 * @brief 提供继电器、光耦隔离输出和蜂鸣器控制能力的设备接口。
 */
typedef struct DevActuator {
    /**
     * @brief 初始化光耦隔离输出和蜂鸣器设备。
     * @param self 接收初始化请求的执行器设备对象。
     */
    void (*init)(struct DevActuator *self);

    /**
     * @brief 设置继电器的吸合或释放状态。
     * @param self 接收控制请求的执行器设备对象。
     * @param enabled 非零表示吸合继电器，零表示释放继电器。
     */
    void (*setRelay)(struct DevActuator *self, uint8_t enabled);

    /**
     * @brief 设置隔离现场输出的有效或关闭状态。
     * @param self 接收控制请求的执行器设备对象。
     * @param enabled 非零表示 24V_IO_OUT 输出约 24 V；零表示输出接近 0 V（关闭）。
     */
    void (*setOptocoupler)(struct DevActuator *self, uint8_t enabled);

    /**
     * @brief 设置蜂鸣器的开启或关闭状态。
     * @param self 接收控制请求的执行器设备对象。
     * @param enabled 非零表示开启蜂鸣器，零表示关闭蜂鸣器。
     */
    void (*setBuzzer)(struct DevActuator *self, uint8_t enabled);

    /**
     * @brief 同时设置 WARNING 与 POWER 指示灯，避免切换时两路状态不一致。
     * @param self 接收控制请求的执行器设备对象。
     * @param warning_enabled 非零表示点亮 WARNING 指示灯，零表示熄灭。
     * @param power_enabled 非零表示点亮 POWER 指示灯，零表示熄灭。
     */
    void (*setIndicatorLeds)(struct DevActuator *self, uint8_t warning_enabled,
                             uint8_t power_enabled);
} DevActuator;

/**
 * @brief 获取继电器、光耦隔离输出和蜂鸣器设备接口。
 * @return 指向静态执行器设备接口的指针。
 */
DevActuator *GetActuator(void);

#endif
