#include "bsp_actuator.h"
#include "bsp_gpio.h"
#include "stm32f103xb.h"

/**
 * @brief 将继电器、光耦和蜂鸣器 GPIO 输出设置为安全的关闭电平。
 */
void BSP_Actuator_Init(void)
{
    /* PA1 高使 PC817 导通并将 24V_IO_OUT 拉低；PA5/PA6 低电平关闭继电器和蜂鸣器。 */
    GPIOA->BSRR = BSP_GPIO_PA_SAFE_HIGH_MASK |
                  (((1UL << BSP_GPIO_PA_RELAY_PIN) |
                    (1UL << BSP_GPIO_PA_BUZZER_PIN)) << BSP_GPIO_BSRR_RESET_SHIFT);
}

/**
 * @brief 按指定状态控制继电器驱动输出。
 * @param enabled 非零时吸合继电器，零时释放继电器。
 */
void BSP_Actuator_SetRelay(uint8_t enabled)
{
    GPIOA->BSRR = enabled != 0U ? (1UL << BSP_GPIO_PA_RELAY_PIN) :
                  (1UL << (BSP_GPIO_PA_RELAY_PIN + BSP_GPIO_BSRR_RESET_SHIFT));
}

/**
 * @brief 按指定状态控制 24V_IO_OUT 隔离现场输出。
 * @param enabled 非零时输出约 24 V，零时输出接近 0 V（关闭）。
 */
void BSP_Actuator_SetOptocoupler(uint8_t enabled)
{
    /* 输出逻辑与 PA1 相反：要得到 24 V 时关闭 PC817 LED；要关闭现场输出时点亮 LED。 */
    GPIOA->BSRR = enabled != 0U ?
                  (1UL << (BSP_GPIO_PA_OPTOCOUPLER_PIN + BSP_GPIO_BSRR_RESET_SHIFT)) :
                  BSP_GPIO_PA_SAFE_HIGH_MASK;
}

/**
 * @brief 按指定状态控制蜂鸣器输出。
 * @param enabled 非零时开启蜂鸣器，零时关闭蜂鸣器。
 */
void BSP_Actuator_SetBuzzer(uint8_t enabled)
{
    GPIOA->BSRR = enabled != 0U ? (1UL << BSP_GPIO_PA_BUZZER_PIN) :
                  (1UL << (BSP_GPIO_PA_BUZZER_PIN + BSP_GPIO_BSRR_RESET_SHIFT));
}

/**
 * @brief 通过单次 BSRR 写入同时更新 WARNING 与 POWER 指示灯。
 * @param warning_enabled 非零时点亮 WARNING 指示灯，零时熄灭。
 * @param power_enabled 非零时点亮 POWER 指示灯，零时熄灭。
 */
void BSP_Actuator_SetIndicatorLeds(uint8_t warning_enabled, uint8_t power_enabled)
{
    uint32_t set_mask = 0U;
    uint32_t reset_mask = 0U;

    if (warning_enabled != 0U) {
        set_mask |= 1UL << BSP_GPIO_PB_WARNING_LED_PIN;
    } else {
        reset_mask |= 1UL << BSP_GPIO_PB_WARNING_LED_PIN;
    }
    if (power_enabled != 0U) {
        set_mask |= 1UL << BSP_GPIO_PB_POWER_LED_PIN;
    } else {
        reset_mask |= 1UL << BSP_GPIO_PB_POWER_LED_PIN;
    }

    /* BSRR 一次写入成对更新两路，避免软件逐个写脚时短暂出现相同状态。 */
    GPIOB->BSRR = set_mask | (reset_mask << BSP_GPIO_BSRR_RESET_SHIFT);
}

