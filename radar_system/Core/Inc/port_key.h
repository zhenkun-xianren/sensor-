#ifndef PORT_KEY_H
#define PORT_KEY_H

#include "dev_key.h"
#include "dev_key_config.h"

/* 按键软件时序：单位毫秒；普通机械按键的可调整默认策略。 */
#define PORT_KEY_SCAN_PERIOD_MS       DEV_KEY_SCAN_PERIOD_MS
#define PORT_KEY_DEBOUNCE_MS          DEV_KEY_DEBOUNCE_MS
#define PORT_KEY_LONG_PRESS_MS        DEV_KEY_LONG_PRESS_MS
/* 超过两次正常扫描间隔就不能再证明输入连续稳定。 */
#define PORT_KEY_MAX_SCAN_GAP_MS      DEV_KEY_MAX_SCAN_GAP_MS
#define PORT_KEY_EVENT_CAPACITY       DEV_KEY_EVENT_CAPACITY

#if PORT_KEY_SCAN_PERIOD_MS == 0 || PORT_KEY_DEBOUNCE_MS < PORT_KEY_SCAN_PERIOD_MS
#error Invalid_key_scan_or_debounce_period
#endif
#if PORT_KEY_LONG_PRESS_MS <= PORT_KEY_DEBOUNCE_MS || PORT_KEY_EVENT_CAPACITY == 0
#error Invalid_key_long_press_or_queue_capacity
#endif

#endif
