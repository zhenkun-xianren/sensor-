#include "bsp_actuator.h"
#include "bsp_gpio_board.h"
#include <stddef.h>

/**
 * @brief 按单脚电平请求原子更新输出锁存。
 * @param gpio 已使能时钟的GPIO端口。
 * @param mask 所属引脚掩码。
 * @param high 0锁存低电平，1锁存高电平。
 * @return 0锁存符合请求，-1锁存异常。
 */
static int actuator_write(GPIO_TypeDef *gpio, uint32_t mask, uint8_t high)
{
    uint32_t expected = high != 0U ? mask : 0U; // 保存请求的ODR锁存值。
    gpio->BSRR = high != 0U ? mask : (mask << BSP_GPIO_BSRR_RESET_SHIFT);
    return (gpio->ODR & mask) == expected ? 0 : -1;
}

/**
 * @brief GPIO准备好后再次建立全部关闭状态，不重新配置GPIO。
 * @return 0表示配置与关闭锁存成功，-1表示失败。
 */
int BSP_Actuator_Init(void)
{
    uint32_t irq_state = __get_PRIMASK(); // 保存初始化前PRIMASK。
    int result; // 保存板级初始化状态。
    __disable_irq();
    result = BSP_Gpio_CheckLocked();
    BSP_Gpio_OffLocked();
    if (result == 0 && ((GPIOB->ODR & BSP_GPIO_B_MASK) != BSP_GPIO_B_HIGH ||
        (GPIOC->ODR & BSP_GPIO_C_MASK) != BSP_GPIO_C_HIGH ||
        (GPIOD->ODR & BSP_GPIO_D_MASK) != BSP_GPIO_D_HIGH)) result = -1;
    if (result != 0) BSP_Gpio_OffLocked(); /* 失败再尽力预置关闭，不宣称负载已经关闭。 */
    __set_PRIMASK(irq_state);
    return result;
}

/**
 * @brief 在配置一致时更新单个执行器GPIO电平，失败尽力关闭全部输出。
 * @param channel 继电器、光耦或蜂鸣器通道。
 * @param high 0低电平，1高电平；逻辑极性由Port转换。
 * @return 0操作成功，-1参数或板级操作失败。
 */
int BSP_Actuator_Set(BspActuatorChannel channel, uint8_t high)
{
    uint32_t irq_state; // 保存控制前PRIMASK。
    int result; // 保存配置和输出检查结果。
    /* 通道从0开始；转为无符号统一拒绝负值和上界外值，避免无符号与0比较警告。 */
    if (high > 1U || (uint32_t)channel > (uint32_t)BSP_ACTUATOR_BUZZER) return -1;
    irq_state = __get_PRIMASK();
    __disable_irq();
    result = BSP_Gpio_CheckLocked();
    if (result == 0) {
        switch (channel) {
        case BSP_ACTUATOR_RELAY:
            result = actuator_write(GPIOC, BSP_GPIO_RELAY_MASK, high);
            break;
        case BSP_ACTUATOR_OPTOCOUPLER:
            result = actuator_write(GPIOC, BSP_GPIO_OPTO_MASK, high);
            break;
        case BSP_ACTUATOR_BUZZER:
            result = actuator_write(GPIOB, BSP_GPIO_BUZZER_MASK, high);
            break;
        default: result = -1; break;
        }
    }
    if (result != 0) BSP_Gpio_OffLocked();
    __set_PRIMASK(irq_state);
    return result;
}

/**
 * @brief 在一次短临界区更新两灯，跨GPIOC/GPIOD不能实现单次硬件原子写入。
 * @param warning_high 报警灯GPIO锁存电平，0点亮、1高阻关闭。
 * @param power_high 电源灯GPIO锁存电平，0点亮、1高阻关闭。
 * @return 0两次写入均成功，-1参数、配置或输出失败；失败尽力关闭全部输出。
 */
int BSP_Actuator_SetIndicatorLevels(uint8_t warning_high, uint8_t power_high)
{
    uint32_t irq_state; // 保存两灯更新前PRIMASK。
    int result; // 保存两次跨端口写入的综合状态。
    int power_result; // 保存电源灯写入结果，避免短路表达式省略第二次写入。
    if (warning_high > 1U || power_high > 1U) return -1;
    irq_state = __get_PRIMASK();
    __disable_irq();
    result = BSP_Gpio_CheckLocked();
    if (result == 0) {
        result = actuator_write(GPIOC, BSP_GPIO_WARNING_MASK, warning_high);
        power_result = actuator_write(GPIOD, BSP_GPIO_POWER_MASK, power_high);
        if (power_result != 0) result = -1;
    }
    if (result != 0) BSP_Gpio_OffLocked();
    __set_PRIMASK(irq_state);
    return result;
}

/**
 * @brief 在一致配置下读取五路ODR快照，失败保持输出对象不变。
 * @param output 非空的GPIO电平输出对象。
 * @return 0读取成功，-1参数或配置错误。
 */
int BSP_Actuator_Get(BspActuatorLevels *output)
{
    uint32_t irq_state; // 保存读取前PRIMASK。
    BspActuatorLevels levels; // 缓存本次读取的全部GPIO电平。
    uint32_t output_b; // 缓存GPIOB输出锁存。
    uint32_t output_c; // 缓存GPIOC输出锁存。
    uint32_t output_d; // 缓存GPIOD输出锁存。
    int result; // 保存配置检查状态。
    if (output == NULL) return -1;
    irq_state = __get_PRIMASK();
    __disable_irq();
    result = BSP_Gpio_CheckLocked();
    if (result == 0) {
        output_b = GPIOB->ODR;
        output_c = GPIOC->ODR;
        output_d = GPIOD->ODR;
        levels.relay = (uint8_t)((output_c >> BSP_GPIO_RELAY_PIN) & 1U);
        levels.optocoupler = (uint8_t)((output_c >> BSP_GPIO_OPTO_PIN) & 1U);
        levels.buzzer = (uint8_t)((output_b >> BSP_GPIO_BUZZER_PIN) & 1U);
        levels.warningLed = (uint8_t)((output_c >> BSP_GPIO_WARNING_PIN) & 1U);
        levels.powerLed = (uint8_t)((output_d >> BSP_GPIO_POWER_PIN) & 1U);
        *output = levels;
    }
    __set_PRIMASK(irq_state);
    return result;
}
