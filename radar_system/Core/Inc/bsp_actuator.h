#ifndef BSP_ACTUATOR_H
#define BSP_ACTUATOR_H
#include "bsp_gpio.h"
#include "dev_actuator_config.h"

/** @brief 接收原始GPIO电平请求的执行器通道。 */
typedef enum {
    BSP_ACTUATOR_RELAY, /**< 继电器线圈驱动。 */
    BSP_ACTUATOR_OPTOCOUPLER, /**< PC817隔离输出。 */
    BSP_ACTUATOR_BUZZER /**< 有源蜂鸣器。 */
} BspActuatorChannel;
/** @brief 五路输出锁存快照，不表示真实负载状态。 */
typedef struct {
    uint8_t relay; // PA5输出锁存电平。
    uint8_t optocoupler; // PA1输出锁存电平。
    uint8_t buzzer; // PA6输出锁存电平。
    uint8_t warningLed; // PB15输出锁存电平。
    uint8_t powerLed; // PB14输出锁存电平。
} BspActuatorLevels;

/**
 * @brief 五路GPIO就绪后建立全部关闭锁存，包括两灯。
 * @return 成功返回0，配置或锁存异常返回-1。
 */
int BSP_Actuator_Init(void);
/**
 * @brief 按原始GPIO电平控制一个执行器通道。
 * @param channel 继电器、光耦或蜂鸣器通道。
 * @param high 0表示低电平，1表示高电平；逻辑极性由Port转换。
 * @return 成功返回0，参数或板级异常返回-1。
 */
int BSP_Actuator_Set(BspActuatorChannel channel, uint8_t high);
/**
 * @brief 使用同一次BSRR写入成对更新来源板卡的两灯电平。
 * @param warning_high 报警灯GPIO电平，0熄灭，1点亮。
 * @param power_high 电源灯GPIO电平，0熄灭，1点亮。
 * @return 成功返回0，参数或板级异常返回-1。
 */
#if DEV_ACTUATOR_ENABLE_AUXILIARY
#if DEV_ACTUATOR_ENABLE_AUXILIARY
int BSP_Actuator_SetIndicatorLevels(uint8_t warning_high, uint8_t power_high);
#endif
#endif
/**
 * @brief 读取五路ODR锁存，失败不修改输出对象。
 * @param output 接收原始电平快照的非空对象。
 * @return 成功返回0，参数或板级异常返回-1。
 */
#if DEV_ACTUATOR_ENABLE_AUXILIARY
#if DEV_ACTUATOR_ENABLE_AUXILIARY
int BSP_Actuator_Get(BspActuatorLevels *output);
#endif
#endif

/* 保留旧逻辑开关入口；void不报告失败，新调用方优先使用Device状态接口。 */
/**
 * @brief 按旧入口请求继电器吸合或释放，内部调用新版电平接口。
 * @param enabled 零释放，非零吸合；该void接口不报告失败。
 */
void BSP_Actuator_SetRelay(uint8_t enabled);
/**
 * @brief 按旧入口请求现场隔离输出，内部调用新版电平接口。
 * @param enabled 零请求现场低电平，非零请求现场高电平。
 */
#if DEV_ACTUATOR_ENABLE_AUXILIARY
#if DEV_ACTUATOR_ENABLE_AUXILIARY
void BSP_Actuator_SetOptocoupler(uint8_t enabled);
#endif
#endif
/**
 * @brief 按旧入口开关有源蜂鸣器，内部调用新版电平接口。
 * @param enabled 零关闭，非零开启。
 */
#if DEV_ACTUATOR_ENABLE_AUXILIARY
#if DEV_ACTUATOR_ENABLE_AUXILIARY
void BSP_Actuator_SetBuzzer(uint8_t enabled);
#endif
#endif
/**
 * @brief 按旧逻辑形式成对设置两灯，保留原入口签名。
 * @param warning_enabled 零熄灭，非零点亮报警灯。
 * @param power_enabled 零熄灭，非零点亮电源灯。
 */
#if DEV_ACTUATOR_ENABLE_AUXILIARY
#if DEV_ACTUATOR_ENABLE_AUXILIARY
void BSP_Actuator_SetIndicatorLeds(uint8_t warning_enabled, uint8_t power_enabled);
#endif
#endif
#endif


