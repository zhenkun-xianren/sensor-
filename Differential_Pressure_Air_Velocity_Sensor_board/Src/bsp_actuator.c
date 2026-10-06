#include "bsp_actuator.h"
#include "bsp_gpio_board.h"
#include <stddef.h>

/**
 * @brief 用BSRR原子写入单路锁存并核对ODR。
 * @param gpio 已开时钟的GPIO端口。
 * @param mask 所属引脚掩码。
 * @param high 零写低电平，非零写高电平。
 * @return 锁存符合请求返回0，否则返回-1。
 */
static int actuator_write(GPIO_TypeDef *gpio, uint32_t mask, uint8_t high)
{
    uint32_t expected = high != BSP_BOARD_LOW ? mask : 0U; // 保存请求的输出锁存值。
    gpio->BSRR = high != BSP_BOARD_LOW ? mask : (mask << BSP_GPIO_BSRR_RESET_SHIFT);
    return (gpio->ODR & mask) == expected ? BSP_BOARD_OK : BSP_BOARD_ERROR;
}

/**
 * @brief 五路GPIO初始化成功后再次确认全部关闭，不重新配置引脚。
 * @return 配置及关闭锁存正常返回0，否则返回-1。
 */
int BSP_Actuator_Init(void)
{
    uint32_t irq_state = __get_PRIMASK(); // 保存调用前中断屏蔽状态。
    int result; // 保存配置及关闭锁存检查结果。
    __disable_irq();
    result = BSP_Gpio_CheckLocked();
    BSP_Gpio_OffLocked();
    if (result == BSP_BOARD_OK && ((GPIOA->ODR & BSP_GPIO_A_MASK) != BSP_GPIO_A_HIGH ||
        (GPIOB->ODR & BSP_GPIO_B_MASK) != BSP_GPIO_B_HIGH)) result = BSP_BOARD_ERROR;
    if (result != BSP_BOARD_OK) BSP_Gpio_OffLocked();
    __set_PRIMASK(irq_state);
    return result;
}

/**
 * @brief 在配置正常时控制单路原始电平，板级失败尽力关闭全部锁存。
 * @param channel 继电器、光耦或蜂鸣器通道。
 * @param high 0低电平，1高电平。
 * @return 操作成功返回0，参数或板级失败返回-1。
 */
int BSP_Actuator_Set(BspActuatorChannel channel, uint8_t high)
{
    uint32_t irq_state; // 保存操作前中断屏蔽状态。
    int result; // 保存配置和输出核对结果。
    if (high > BSP_BOARD_HIGH || (uint32_t)channel > (uint32_t)BSP_ACTUATOR_BUZZER) return BSP_BOARD_ERROR;
    irq_state = __get_PRIMASK();
    __disable_irq();
    result = BSP_Gpio_CheckLocked();
    if (result == BSP_BOARD_OK) {
        switch (channel) {
        case BSP_ACTUATOR_RELAY:
            result = actuator_write(GPIOB, BSP_GPIO_RELAY_MASK, high);
            break;
        case BSP_ACTUATOR_OPTOCOUPLER:
            result = actuator_write(GPIOB, BSP_GPIO_OPTO_MASK, high);
            break;
        case BSP_ACTUATOR_BUZZER:
            result = actuator_write(GPIOA, BSP_GPIO_BUZZER_MASK, high);
            break;
        default: result = BSP_BOARD_ERROR; break;
        }
    }
    if (result != BSP_BOARD_OK) BSP_Gpio_OffLocked();
    __set_PRIMASK(irq_state);
    return result;
}

/**
 * @brief 在同一短临界区通过一次GPIOB的BSRR写入更新两灯。
 * @param warning_high 报警灯GPIO电平，0拉低点亮，1释放熄灭。
 * @param power_high 电源灯GPIO电平，0拉低点亮，1释放熄灭。
 * @return 成功返回0，参数或板级失败返回-1；板级失败尽力关闭全部锁存。
 */
int BSP_Actuator_SetIndicatorLevels(uint8_t warning_high, uint8_t power_high)
{
    uint32_t irq_state; // 保存成对更新前中断屏蔽状态。
    uint32_t expected; // 保存两灯目标锁存组合。
    int result; // 保存配置和输出核对结果。
    if (warning_high > BSP_BOARD_HIGH || power_high > BSP_BOARD_HIGH) return BSP_BOARD_ERROR;
    expected = ((uint32_t)warning_high << BSP_GPIO_WARNING_PIN) | ((uint32_t)power_high << BSP_GPIO_POWER_PIN);
    irq_state = __get_PRIMASK();
    __disable_irq();
    result = BSP_Gpio_CheckLocked();
    if (result == BSP_BOARD_OK) {
        GPIOB->BSRR = expected | ((BSP_GPIO_LIGHT_MASK & ~expected) << BSP_GPIO_BSRR_RESET_SHIFT);
        if ((GPIOB->ODR & BSP_GPIO_LIGHT_MASK) != expected) result = BSP_BOARD_ERROR;
    }
    if (result != BSP_BOARD_OK) BSP_Gpio_OffLocked();
    __set_PRIMASK(irq_state);
    return result;
}

/**
 * @brief 在配置正常时读取五路ODR快照，仅成功时更新输出对象。
 * @param output 接收原始锁存电平的非空对象，不表示真实负载反馈。
 * @return 成功返回0，参数或板级失败返回-1。
 */
int BSP_Actuator_Get(BspActuatorLevels *output)
{
    uint32_t irq_state; // 保存快照读取前中断屏蔽状态。
    uint32_t output_a; // 保存GPIOA输出锁存快照。
    uint32_t output_b; // 保存GPIOB输出锁存快照。
    BspActuatorLevels levels; // 缓存五路原始锁存电平。
    int result; // 保存配置检查结果。
    if (output == NULL) return BSP_BOARD_ERROR;
    irq_state = __get_PRIMASK();
    __disable_irq();
    result = BSP_Gpio_CheckLocked();
    if (result == BSP_BOARD_OK) {
        output_a = GPIOA->ODR;
        output_b = GPIOB->ODR;
        levels.relay = (uint8_t)((output_b >> BSP_GPIO_RELAY_PIN) & BSP_BOARD_HIGH);
        levels.optocoupler = (uint8_t)((output_b >> BSP_GPIO_OPTO_PIN) & BSP_BOARD_HIGH);
        levels.buzzer = (uint8_t)((output_a >> BSP_GPIO_BUZZER_PIN) & BSP_BOARD_HIGH);
        levels.warningLed = (uint8_t)((output_b >> BSP_GPIO_WARNING_PIN) & BSP_BOARD_HIGH);
        levels.powerLed = (uint8_t)((output_b >> BSP_GPIO_POWER_PIN) & BSP_BOARD_HIGH);
        *output = levels;
    }
    __set_PRIMASK(irq_state);
    return result;
}

/**
 * @brief 保留旧继电器逻辑入口，通过新版电平接口操作。
 * @param enabled 零释放，非零吸合；该void入口不报告失败。
 */
void BSP_Actuator_SetRelay(uint8_t enabled)
{
    (void)BSP_Actuator_Set(BSP_ACTUATOR_RELAY, enabled != BSP_BOARD_LOW ? BSP_GPIO_RELAY_ACTIVE : BSP_GPIO_RELAY_OFF);
}
/**
 * @brief 保留旧光耦逻辑入口，现场输出与PB15电平反相。
 * @param enabled 零请求现场低电平，非零请求现场高电平。
 */
void BSP_Actuator_SetOptocoupler(uint8_t enabled)
{
    (void)BSP_Actuator_Set(BSP_ACTUATOR_OPTOCOUPLER, enabled != BSP_BOARD_LOW ? BSP_GPIO_OPTO_ACTIVE : BSP_GPIO_OPTO_OFF);
}
/**
 * @brief 保留旧蜂鸣器逻辑入口，通过新版电平接口操作。
 * @param enabled 零关闭，非零开启。
 */
void BSP_Actuator_SetBuzzer(uint8_t enabled)
{
    (void)BSP_Actuator_Set(BSP_ACTUATOR_BUZZER, enabled != BSP_BOARD_LOW ? BSP_GPIO_BUZZER_ACTIVE : BSP_GPIO_BUZZER_OFF);
}
/**
 * @brief 保留旧两灯逻辑入口，通过新版电平接口成对更新。
 * @param warning_enabled 零熄灭，非零点亮报警灯。
 * @param power_enabled 零熄灭，非零点亮电源灯。
 */
void BSP_Actuator_SetIndicatorLeds(uint8_t warning_enabled, uint8_t power_enabled)
{
    (void)BSP_Actuator_SetIndicatorLevels(warning_enabled != BSP_BOARD_LOW ? BSP_GPIO_WARNING_ACTIVE : BSP_GPIO_WARNING_OFF,
        power_enabled != BSP_BOARD_LOW ? BSP_GPIO_POWER_ACTIVE : BSP_GPIO_POWER_OFF);
}
