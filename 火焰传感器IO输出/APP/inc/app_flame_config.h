#ifndef APP_FLAME_CONFIG_H
#define APP_FLAME_CONFIG_H

#include "app_flame_detection.h"

/** @brief 三路用户授权的调试起始配置；supplied=false时跳过该路并上报无法判断。 */
extern const AppFlameSetup g_app_flame_setup[DEV_FLAME_CHANNEL_COUNT];

#endif
