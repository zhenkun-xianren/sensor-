#ifndef DEV_KEY_CONFIG_H
#define DEV_KEY_CONFIG_H

/* 普通机械按键的软件策略，单位毫秒；APP和Port引用同一份配置。 */
#define DEV_KEY_SCAN_PERIOD_MS       10U
#define DEV_KEY_DEBOUNCE_MS          20U
#define DEV_KEY_LONG_PRESS_MS        800U
#define DEV_KEY_MAX_SCAN_GAP_MS      (DEV_KEY_SCAN_PERIOD_MS * 2U)
#define DEV_KEY_EVENT_CAPACITY       16U

#endif
