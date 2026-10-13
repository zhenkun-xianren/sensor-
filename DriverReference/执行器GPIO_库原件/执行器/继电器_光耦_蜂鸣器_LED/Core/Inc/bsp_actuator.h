#ifndef BSP_ACTUATOR_H
#define BSP_ACTUATOR_H

#include <stdint.h>

void BSP_Actuator_Init(void);
void BSP_Actuator_SetRelay(uint8_t enabled);
void BSP_Actuator_SetOptocoupler(uint8_t enabled);
void BSP_Actuator_SetBuzzer(uint8_t enabled);
void BSP_Actuator_SetIndicatorLeds(uint8_t warning_enabled, uint8_t power_enabled);

#endif
