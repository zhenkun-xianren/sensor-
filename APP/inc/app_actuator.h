#ifndef APP_ACTUATOR_H
#define APP_ACTUATOR_H
#include "dev_actuator.h"
DevStatus AppActuator_Init(void);
DevStatus AppActuator_SetRelay(uint8_t enabled);
DevStatus AppActuator_SetOptocoupler(uint8_t enabled);
DevStatus AppActuator_SetBuzzer(uint8_t enabled);
DevStatus AppActuator_SetIndicatorLeds(uint8_t warning_enabled, uint8_t power_enabled);
DevStatus AppActuator_GetState(DevActuatorState *output);
#endif
