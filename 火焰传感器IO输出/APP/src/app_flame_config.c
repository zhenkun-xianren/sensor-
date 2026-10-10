#include "app_flame_config.h"

/* 用户已授权三路采用以下调试起始值并启用；这些值不是厂家默认值，
 * 火焰距离及干扰表现仍需实物验证。检测只使用内置事件源。 */
enum {
    APP_FLAME_START_SENSITIVITY_CODE = 32, /**< 用户指定阈值，名义约312微伏，探测及干扰表现待实测。 */
    APP_FLAME_START_BLIND_CODE = 0, /**< 名义盲锁0.5秒，APP另加保守裕量。 */
    APP_FLAME_START_PULSE_CODE = 2, /**< 窗口内需4个交替极性脉冲，整体触发只计1次事件。 */
    APP_FLAME_START_WINDOW_CODE = 0 /**< 名义检测窗口2秒。 */
};

/** @brief 三路共用的完整调试起始配置，避免分路遗漏字段。 */
#define APP_FLAME_START_DETECTOR { \
    .sensitivity = APP_FLAME_START_SENSITIVITY_CODE, \
    .blind_time_code = APP_FLAME_START_BLIND_CODE, \
    .pulse_count_code = APP_FLAME_START_PULSE_CODE, \
    .window_time_code = APP_FLAME_START_WINDOW_CODE, \
    .detection_enabled = true, \
    .interrupt_source = DEV_FLAME_INTERRUPT_EVENT, \
    .adc_source = DEV_FLAME_SOURCE_BPF, \
    .high_pass = DEV_FLAME_HPF_0P4_HZ, \
    .pulse_mode = DEV_FLAME_COUNT_ALTERNATING \
}

const AppFlameSetup g_app_flame_setup[DEV_FLAME_CHANNEL_COUNT] = {
    {.supplied = true, .detector = APP_FLAME_START_DETECTOR}, /* U22：启动启用。 */
    {.supplied = true, .detector = APP_FLAME_START_DETECTOR}, /* U23：启动启用。 */
    {.supplied = true, .detector = APP_FLAME_START_DETECTOR}  /* U24：启动启用。 */
}; // 与固定通道顺序对应的三路完整配置。
