#ifndef DEV_ACTUATOR_CONFIG_H
#define DEV_ACTUATOR_CONFIG_H
/* 本板仅启用现场光耦；其他完整库功能保留源码但不参与编译。
 * 恢复前必须先适配对应BSP和正式引脚映射，不能直接打开开关。 */
#define DEV_ACTUATOR_ENABLE_RELAY          0
#define DEV_ACTUATOR_ENABLE_OPTOCOUPLER    1
#define DEV_ACTUATOR_ENABLE_BUZZER         0
#define DEV_ACTUATOR_ENABLE_INDICATOR_LEDS 0
#define DEV_ACTUATOR_ENABLE_STATE          0
#define DEV_ACTUATOR_ENABLE_LEGACY         0
#endif
