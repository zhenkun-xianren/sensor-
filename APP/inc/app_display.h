#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include <stdint.h>

typedef enum {
    APP_DISPLAY_INIT_FAULT,
    APP_DISPLAY_DIAGNOSTIC,
    APP_DISPLAY_REMOTE,
    APP_DISPLAY_SENSOR_ERROR,
    APP_DISPLAY_CONCENTRATION
} AppDisplayChoice;

AppDisplayChoice AppDisplay_Choose(uint8_t init_fault, uint8_t diagnostic,
                                    uint8_t remote_visible, uint8_t sensor_error);

uint8_t AppDisplay_Init(void);

#endif
