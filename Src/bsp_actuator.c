#include "bsp_actuator.h"
#include "bsp_gpio.h"
#include "stm32f103xb.h"

/**
 * @brief 将继电器、光耦和蜂鸣器 GPIO 输出设置为安全的关闭电平。
 * @return 安全初值和两灯正确返回 1，否则返回 0。
 */
uint8_t BSP_Actuator_Init(void)
{
    /* 复用已验证的手写安全 GPIO 初始化；不得触碰数码管及 LED 引脚。 */
    BSP_Gpio_InitSafeOutputs();
    return (BSP_Gpio_SafeOutputsAreOff() != 0U &&
            BSP_Gpio_IndicatorsAreOn() != 0U) ? 1U : 0U;
}

/**
 * @brief 按指定状态控制继电器驱动输出。
 * @param enabled 非零时吸合继电器，零时释放继电器。
 * @return 输出锁存状态与要求一致返回 1，否则返回 0。
 */
uint8_t BSP_Actuator_SetRelay(uint8_t enabled)
{
    GPIOA->BSRR = enabled != 0U ? (1UL << BSP_GPIO_PA_RELAY_PIN) :
                  (1UL << (BSP_GPIO_PA_RELAY_PIN + BSP_GPIO_BSRR_RESET_SHIFT));
    return (((GPIOA->ODR >> BSP_GPIO_PA_RELAY_PIN) & 1U) ==
            (enabled != 0U ? 1U : 0U)) ? 1U : 0U;
}

/**
 * @brief 按指定状态控制 24V_IO_OUT 隔离现场输出。
 * @param enabled 非零时输出约 24 V，零时输出接近 0 V（关闭）。
 * @return MCU 控制脚锁存状态与要求一致返回 1，否则返回 0。
 */
uint8_t BSP_Actuator_SetOptocoupler(uint8_t enabled)
{
    /* 输出逻辑与 PA1 相反：要得到 24 V 时关闭 PC817 LED；要关闭现场输出时点亮 LED。 */
    GPIOA->BSRR = enabled != 0U ?
                  (1UL << (BSP_GPIO_PA_OPTOCOUPLER_PIN + BSP_GPIO_BSRR_RESET_SHIFT)) :
                  BSP_GPIO_PA_SAFE_HIGH_MASK;
    return (((GPIOA->ODR >> BSP_GPIO_PA_OPTOCOUPLER_PIN) & 1U) ==
            (enabled != 0U ? 0U : 1U)) ? 1U : 0U;
}

/**
 * @brief 按指定状态控制蜂鸣器输出。
 * @param enabled 非零时开启蜂鸣器，零时关闭蜂鸣器。
 * @return 输出锁存状态与要求一致返回 1，否则返回 0。
 */
uint8_t BSP_Actuator_SetBuzzer(uint8_t enabled)
{
    GPIOA->BSRR = enabled != 0U ? (1UL << BSP_GPIO_PA_BUZZER_PIN) :
                  (1UL << (BSP_GPIO_PA_BUZZER_PIN + BSP_GPIO_BSRR_RESET_SHIFT));
    return (((GPIOA->ODR >> BSP_GPIO_PA_BUZZER_PIN) & 1U) ==
            (enabled != 0U ? 1U : 0U)) ? 1U : 0U;
}

/**
 * @brief 通过单次 BSRR 写入同时更新 WARNING 与 POWER 指示灯。
 * @param warning_enabled 非零时点亮 WARNING 指示灯，零时熄灭。
 * @param power_enabled 非零时点亮 POWER 指示灯，零时熄灭。
 * @return 两路输出锁存状态与要求一致返回 1，否则返回 0。
 */
uint8_t BSP_Actuator_SetIndicatorLeds(uint8_t warning_enabled, uint8_t power_enabled)
{
    uint32_t set_mask = 0U; // 本次需要置位的 GPIO 输出掩码。
    uint32_t reset_mask = 0U; // 本次需要复位的 GPIO 输出掩码。

    if (warning_enabled != 0U) {
        reset_mask |= 1UL << BSP_GPIO_PB_WARNING_LED_PIN;
    } else {
        set_mask |= 1UL << BSP_GPIO_PB_WARNING_LED_PIN;
    }
    if (power_enabled != 0U) {
        reset_mask |= 1UL << BSP_GPIO_PB_POWER_LED_PIN;
    } else {
        set_mask |= 1UL << BSP_GPIO_PB_POWER_LED_PIN;
    }

    /* 两灯低电平点亮；BSRR 单次写入可避免两灯更新间的瞬态。 */
    GPIOB->BSRR = set_mask | (reset_mask << BSP_GPIO_BSRR_RESET_SHIFT);
    return ((GPIOB->ODR & (set_mask | reset_mask)) == set_mask) ? 1U : 0U;
}

