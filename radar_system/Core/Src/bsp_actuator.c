#include "bsp_actuator.h"
#include "bsp_gpio_board.h"
/**
 * @brief 原子写入所属GPIO锁存并核对结果。
 * @param gpio 已开时钟的GPIO端口。
 * @param mask 所属引脚掩码。
 * @param high 请求的原始电平。
 * @return 符合请求返回BSP_GPIO_OK，否则返回BSP_GPIO_ERROR。
 */
static int actuator_write(GPIO_TypeDef *gpio, uint32_t mask, uint8_t high)
{
    uint32_t expected = high == BSP_GPIO_HIGH ? mask : 0U; // 保存目标锁存位。
    gpio->BSRR = high == BSP_GPIO_HIGH ? mask : (mask << BSP_GPIO_BSRR_RESET_SHIFT);
    return (gpio->ODR & mask) == expected ? BSP_GPIO_OK : BSP_GPIO_ERROR;
}
/**
 * @brief GPIO准备后检查继电器配置并建立关闭锁存。
 * @return 配置及锁存正常返回BSP_GPIO_OK，否则返回BSP_GPIO_ERROR。
 */
int BSP_Actuator_Init(void)
{
    uint32_t irq_state = __get_PRIMASK(); // 保存调用前中断屏蔽状态。
    int result; // 保存配置及锁存检查结果。
    __disable_irq();
    result = BSP_Gpio_CheckLocked();
    BSP_Gpio_OffLocked();
    if ((GPIOB->ODR & BSP_GPIO_RELAY_MASK) != 0U) result = BSP_GPIO_ERROR;
    __set_PRIMASK(irq_state);
    return result;
}
/**
 * @brief 控制继电器原始电平，不访问本板未提供的其他执行器。
 * @param channel 必须为BSP_ACTUATOR_RELAY。
 * @param high BSP_GPIO_LOW或BSP_GPIO_HIGH。
 * @return 成功返回BSP_GPIO_OK，否则返回BSP_GPIO_ERROR。
 */
int BSP_Actuator_Set(BspActuatorChannel channel, uint8_t high)
{
    uint32_t irq_state; // 保存操作前中断屏蔽状态。
    int result; // 保存配置及写入结果。
    if (channel != BSP_ACTUATOR_RELAY || high > BSP_GPIO_HIGH) return BSP_GPIO_ERROR;
    irq_state = __get_PRIMASK();
    __disable_irq();
    result = BSP_Gpio_CheckLocked();
    if (result == BSP_GPIO_OK) result = actuator_write(GPIOB, BSP_GPIO_RELAY_MASK, high);
    if (result != BSP_GPIO_OK) BSP_Gpio_OffLocked();
    __set_PRIMASK(irq_state);
    return result;
}
/**
 * @brief 保留库内旧继电器逻辑入口，调用方优先使用Device状态接口。
 * @param enabled 零释放，非零请求吸合。
 */
void BSP_Actuator_SetRelay(uint8_t enabled)
{
    (void)BSP_Actuator_Set(BSP_ACTUATOR_RELAY,
        enabled != 0U ? BSP_GPIO_RELAY_ACTIVE : BSP_GPIO_RELAY_OFF);
}

/* 未使用的来源板BSP完整保留；恢复前必须适配正式引脚及极性。 */
#if DEV_ACTUATOR_ENABLE_AUXILIARY
/**
 * @brief 在同一短临界区通过一次GPIOB的BSRR写入更新两灯。
 * @param warning_high 报警灯GPIO电平，0熄灭，1点亮。
 * @param power_high 电源灯GPIO电平，0熄灭，1点亮。
 * @return 成功返回0，参数或板级失败返回-1；板级失败尽力关闭全部锁存。
 */
int BSP_Actuator_SetIndicatorLevels(uint8_t warning_high, uint8_t power_high)
{
    uint32_t irq_state; // 保存成对更新前中断屏蔽状态。
    uint32_t expected; // 保存两灯目标锁存组合。
    int result; // 保存配置和输出核对结果。
    if (warning_high > 1U || power_high > 1U) return -1;
    expected = ((uint32_t)warning_high << BSP_GPIO_WARNING_PIN) | ((uint32_t)power_high << BSP_GPIO_POWER_PIN);
    irq_state = __get_PRIMASK();
    __disable_irq();
    result = BSP_Gpio_CheckLocked();
    if (result == 0) {
        GPIOB->BSRR = expected | ((BSP_GPIO_B_MASK & ~expected) << BSP_GPIO_BSRR_RESET_SHIFT);
        if ((GPIOB->ODR & BSP_GPIO_B_MASK) != expected) result = -1;
    }
    if (result != 0) BSP_Gpio_OffLocked();
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
    if (output == NULL) return -1;
    irq_state = __get_PRIMASK();
    __disable_irq();
    result = BSP_Gpio_CheckLocked();
    if (result == 0) {
        output_a = GPIOA->ODR;
        output_b = GPIOB->ODR;
        levels.relay = (uint8_t)((output_a >> BSP_GPIO_RELAY_PIN) & 1U);
        levels.optocoupler = (uint8_t)((output_a >> BSP_GPIO_OPTO_PIN) & 1U);
        levels.buzzer = (uint8_t)((output_a >> BSP_GPIO_BUZZER_PIN) & 1U);
        levels.warningLed = (uint8_t)((output_b >> BSP_GPIO_WARNING_PIN) & 1U);
        levels.powerLed = (uint8_t)((output_b >> BSP_GPIO_POWER_PIN) & 1U);
        *output = levels;
    }
    __set_PRIMASK(irq_state);
    return result;
}
/**
 * @brief 保留旧光耦逻辑入口，现场输出与PA1电平反相。
 * @param enabled 零请求现场低电平，非零请求现场高电平。
 */
void BSP_Actuator_SetOptocoupler(uint8_t enabled)
{
    (void)BSP_Actuator_Set(BSP_ACTUATOR_OPTOCOUPLER, enabled != 0U ? BSP_GPIO_OPTO_ACTIVE : BSP_GPIO_OPTO_OFF);
}
/**
 * @brief 保留旧蜂鸣器逻辑入口，通过新版电平接口操作。
 * @param enabled 零关闭，非零开启。
 */
void BSP_Actuator_SetBuzzer(uint8_t enabled)
{
    (void)BSP_Actuator_Set(BSP_ACTUATOR_BUZZER, enabled != 0U ? BSP_GPIO_BUZZER_ACTIVE : BSP_GPIO_BUZZER_OFF);
}
/**
 * @brief 保留旧两灯逻辑入口，通过新版电平接口成对更新。
 * @param warning_enabled 零熄灭，非零点亮报警灯。
 * @param power_enabled 零熄灭，非零点亮电源灯。
 */
void BSP_Actuator_SetIndicatorLeds(uint8_t warning_enabled, uint8_t power_enabled)
{
    (void)BSP_Actuator_SetIndicatorLevels(warning_enabled != 0U ? BSP_GPIO_WARNING_ACTIVE : BSP_GPIO_WARNING_OFF,
        power_enabled != 0U ? BSP_GPIO_POWER_ACTIVE : BSP_GPIO_POWER_OFF);
}
#endif

