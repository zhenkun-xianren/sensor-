#ifndef BSP_ACTUATOR_H
#define BSP_ACTUATOR_H
#include "bsp_gpio.h"

typedef enum {
    BSP_ACTUATOR_RELAY,
    BSP_ACTUATOR_OPTOCOUPLER,
    BSP_ACTUATOR_BUZZER
} BspActuatorChannel;
typedef struct {
    uint8_t relay; // PC6输出锁存电平。
    uint8_t optocoupler; // PC7输出锁存电平。
    uint8_t buzzer; // PB14输出锁存电平。
    uint8_t warningLed; // PC9输出锁存电平。
    uint8_t powerLed; // PD1输出锁存电平。
} BspActuatorLevels;
int BSP_Actuator_Init(void);
int BSP_Actuator_Set(BspActuatorChannel channel, uint8_t high);
int BSP_Actuator_SetIndicatorLevels(uint8_t warning_high, uint8_t power_high);
int BSP_Actuator_Get(BspActuatorLevels *output);
#endif
