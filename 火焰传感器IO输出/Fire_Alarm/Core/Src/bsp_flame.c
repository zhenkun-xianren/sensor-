#include <stddef.h>
#include "stm32f0xx.h"
#include "bsp_flame.h"

/** @brief 同一路两个信号在同一端口；字段均由正式表及 CMSIS 定义。 */
typedef struct {
    GPIO_TypeDef *gpio;      // 本路端口，仅操作所属两脚。
    uint32_t clock;          // 对应 AHB GPIO 时钟位。
    uint32_t serin;          // SERIN 输出掩码。
    uint32_t doci;           // DOCI 双向信号掩码。
    uint32_t fields;         // 两脚 MODER/OSPEEDR/PUPDR 两位字段总掩码。
    uint32_t serin_mode;     // SERIN 普通输出字段。
    uint32_t doci_field;     // DOCI 模式字段掩码。
    uint32_t doci_mode;      // DOCI 普通输出字段。
    uint32_t speed;          // 两脚中速输出配置，避免慢边沿影响短脉冲。
} BspFlamePins;

static const BspFlamePins s_flame_pins[DEV_FLAME_CHANNEL_COUNT] = {
    {BSP_FLAME_U22_PORT, BSP_FLAME_U22_CLOCK, BSP_FLAME_U22_SERIN, BSP_FLAME_U22_DOCI,
     BSP_FLAME_U22_FIELDS, BSP_FLAME_U22_SERIN_MODE, BSP_FLAME_U22_DOCI_FIELD,
     BSP_FLAME_U22_DOCI_MODE, BSP_FLAME_U22_SPEED},
    {BSP_FLAME_U23_PORT, BSP_FLAME_U23_CLOCK, BSP_FLAME_U23_SERIN, BSP_FLAME_U23_DOCI,
     BSP_FLAME_U23_FIELDS, BSP_FLAME_U23_SERIN_MODE, BSP_FLAME_U23_DOCI_FIELD,
     BSP_FLAME_U23_DOCI_MODE, BSP_FLAME_U23_SPEED},
    {BSP_FLAME_U24_PORT, BSP_FLAME_U24_CLOCK, BSP_FLAME_U24_SERIN, BSP_FLAME_U24_DOCI,
     BSP_FLAME_U24_FIELDS, BSP_FLAME_U24_SERIN_MODE, BSP_FLAME_U24_DOCI_FIELD,
     BSP_FLAME_U24_DOCI_MODE, BSP_FLAME_U24_SPEED}
}; // 正式表 V1.2 对应的三个静态板级实例。
static bool s_flame_initialized[DEV_FLAME_CHANNEL_COUNT]; // 本路 GPIO 是否完成配置。
static bool s_flame_time_ready = false; // TIM2 是否由本模块初始化。
static volatile bool s_flame_busy = false; // 三路共享总线使用权，不等同于 Port 方法互斥。

/** @brief 根据合法通道查所属引脚，先校验再索引数组。
 * @param channel 逻辑通道。
 * @return 引脚定义；非法通道返回 NULL。 */
static const BspFlamePins *bsp_flame_get_pins(DevFlameChannel channel)
{
    if ((channel != DEV_FLAME_U22) && (channel != DEV_FLAME_U23) && (channel != DEV_FLAME_U24)) {
        return NULL;
    }
    return &s_flame_pins[channel];
}

/** @brief 取得三路传输锁并屏蔽中断；调用方随后可在长等待中恢复 PRIMASK。
 * @param saved_primask 接收调用前中断屏蔽状态。
 * @return OK 或 ARGUMENT_ERROR/BUSY；失败不会改变原中断状态。 */
static DevFlameResult bsp_flame_lock(uint32_t *saved_primask)
{
    uint32_t previous; // 仅保护共享使用权和 GPIO 的短临界区。
    if (saved_primask == NULL) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    if (__get_IPSR() != BSP_FLAME_THREAD_MODE) {
        return DEV_FLAME_BUSY;
    }
    previous = __get_PRIMASK();
    __disable_irq();
    if (s_flame_busy) {
        __set_PRIMASK(previous);
        return DEV_FLAME_BUSY;
    }
    s_flame_busy = true;
    *saved_primask = previous;
    return DEV_FLAME_OK;
}

/** @brief 结束共享总线操作，恢复调用前的中断屏蔽状态。
 * @param saved_primask 本次锁成功时保存的 PRIMASK。 */
static void bsp_flame_unlock(uint32_t saved_primask)
{
    __disable_irq();
    s_flame_busy = false;
    __set_PRIMASK(saved_primask);
}

/** @brief 在专属自由运行计数器上有界等待，不依赖 SysTick 或 RTOS。
 * @param delay_us 不超过 WAIT_MAX_US 的微秒数。
 * @return OK 或参数、计时基准错误、停转超时。 */
static DevFlameResult bsp_flame_wait_us(uint32_t delay_us)
{
    uint32_t start; // 无符号差允许 TIM2 自然回绕。
    uint32_t remaining; // 停转时的轮询次数上限，频率由板级定义推导。
    if (delay_us > BSP_FLAME_WAIT_MAX_US) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    if (!s_flame_time_ready ||
        ((RCC->APB1ENR & RCC_APB1ENR_TIM2EN) == BSP_FLAME_FLAG_ABSENT) ||
        ((TIM2->CR1 & TIM_CR1_CEN) == BSP_FLAME_FLAG_ABSENT)) {
        return DEV_FLAME_IO_ERROR;
    }
    start = TIM2->CNT;
    remaining = delay_us * (BSP_BOARD_HCLK_HZ / BSP_FLAME_COUNTER_HZ) + BSP_FLAME_WAIT_MARGIN;
    do {
        if ((uint32_t)(TIM2->CNT - start) >= delay_us) {
            return DEV_FLAME_OK;
        }
    } while (--remaining != 0);
    return DEV_FLAME_TIMEOUT;
}

/** @brief 按实际板级不分频时钟建立 TIM2 1MHz 基准，拒绝夺取已启用资源。
 * @return OK 或时钟/寄存器错误、资源占用、计数停转超时。 */
static DevFlameResult bsp_flame_time_init(void)
{
    uint32_t prescaler = BSP_FLAME_TIM2_HZ / BSP_FLAME_COUNTER_HZ - 1UL; // PSC+1 分频。
    DevFlameResult result; // 保存初始化计数探测结果。
    if ((BSP_BOARD_HCLK_HZ != BSP_FLAME_PULSE_HCLK_HZ) ||
        (SystemCoreClock != BSP_BOARD_HCLK_HZ) ||
        (BSP_FLAME_TIM2_HZ % BSP_FLAME_COUNTER_HZ != 0) ||
        ((RCC->CFGR & RCC_CFGR_HPRE) != RCC_CFGR_HPRE_DIV1) ||
        ((RCC->CFGR & RCC_CFGR_PPRE) != RCC_CFGR_PPRE_DIV1) ||
        ((FLASH->ACR & FLASH_ACR_LATENCY) != BSP_BOARD_FLASH_LATENCY)) {
        return DEV_FLAME_IO_ERROR;
    }
    if (!s_flame_time_ready) {
        if ((RCC->APB1ENR & RCC_APB1ENR_TIM2EN) != BSP_FLAME_FLAG_ABSENT) {
            return DEV_FLAME_BUSY;
        }
        RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
        (void)RCC->APB1ENR; // AHB/APB 时钟开通后先完成读回同步。
        RCC->APB1RSTR |= RCC_APB1RSTR_TIM2RST;
        RCC->APB1RSTR &= ~RCC_APB1RSTR_TIM2RST;
        TIM2->CR1 = BSP_FLAME_REGISTER_CLEAR;
        TIM2->PSC = prescaler;
        TIM2->ARR = BSP_FLAME_COUNTER_MAX;
        TIM2->EGR = TIM_EGR_UG; // 立即装载 PSC；不启用中断和 DMA。
        TIM2->SR = BSP_FLAME_REGISTER_CLEAR; // 此时专属计数器的更新标志写零清除。
        TIM2->CNT = BSP_FLAME_REGISTER_CLEAR;
        TIM2->CR1 = TIM_CR1_CEN;
        s_flame_time_ready = true;
    }
    if (((RCC->APB1ENR & RCC_APB1ENR_TIM2EN) == BSP_FLAME_FLAG_ABSENT) ||
        (TIM2->CR1 != TIM_CR1_CEN) || (TIM2->PSC != prescaler) ||
        (TIM2->ARR != BSP_FLAME_COUNTER_MAX) ||
        (TIM2->SMCR != BSP_FLAME_REGISTER_CLEAR) ||
        (TIM2->DIER != BSP_FLAME_REGISTER_CLEAR)) {
        return DEV_FLAME_IO_ERROR;
    }
    result = bsp_flame_wait_us(BSP_FLAME_COUNTER_PROBE_US);
    return result;
}

/** @brief 通过 BSRR 原子修改 SERIN，避免端口整字读改写。
 * @param pins 本路引脚定义。
 * @param high 要建立的电平。 */
__STATIC_FORCEINLINE void bsp_flame_serin_set(const BspFlamePins *pins, bool high)
{
    pins->gpio->BSRR = high ? pins->serin : (pins->serin << BSP_FLAME_BSRR_RESET_SHIFT);
}

/** @brief 先预置 DOCI 电平，再切换到普通推挽输出；须在锁内屏蔽中断调用。
 * @param pins 本路引脚定义。
 * @param high 要驱动的电平。 */
__STATIC_FORCEINLINE void bsp_flame_doci_drive(const BspFlamePins *pins, bool high)
{
    pins->gpio->BSRR = high ? pins->doci : (pins->doci << BSP_FLAME_BSRR_RESET_SHIFT);
    pins->gpio->MODER = (pins->gpio->MODER & ~pins->doci_field) | pins->doci_mode;
}

/** @brief DOCI 释放为无上下拉输入，不覆盖 SERIN 或其他模块模式。
 * @param pins 本路引脚定义；须在锁内屏蔽中断调用。 */
__STATIC_FORCEINLINE void bsp_flame_doci_release(const BspFlamePins *pins)
{
    pins->gpio->MODER &= ~pins->doci_field;
}

/** @brief 采样 DOCI 电平，不修改中断源或主动清除事件。
 * @param pins 本路引脚定义。
 * @return 引脚为高返回 true，否则 false。 */
__STATIC_FORCEINLINE bool bsp_flame_doci_level(const BspFlamePins *pins)
{
    return (pins->gpio->IDR & pins->doci) != BSP_FLAME_FLAG_ABSENT;
}

/** @brief 发出一个低/高短脉冲并保持配置位；编译后核对 NOP 和寄存器间指令。
 * @param pins 本路引脚定义，当前 SERIN 已为输出。
 * @param bit 要写入的位；整个帧的短脉冲阶段须屏蔽中断。
 * @return OK 或 TIM2 计数停转 TIMEOUT。 */
__STATIC_FORCEINLINE DevFlameResult bsp_flame_write_bit(const BspFlamePins *pins, bool bit)
{
    GPIO_TypeDef *gpio = pins->gpio; // 预先取得地址，短脉冲之间不调用普通函数。
    uint32_t high = pins->serin; // 时钟高电平命令。
    uint32_t low = high << BSP_FLAME_BSRR_RESET_SHIFT; // 时钟低电平命令。
    uint32_t data = bit ? high : low; // 在时钟建立前选择位电平，避免脉冲内分支。
    uint32_t start = TIM2->CNT; // 单位驻留起点。
    uint32_t remaining = BSP_FLAME_BIT_POLL_LIMIT; // 定时器停转时有界退出。
    gpio->BSRR = low;
    __NOP(); __NOP(); __NOP(); __NOP(); // 8MHz 下至少 0.5us，另含寄存器访问开销。
    gpio->BSRR = high;
    __NOP(); __NOP(); __NOP(); __NOP();
    gpio->BSRR = data;
    do {
        uint32_t elapsed = TIM2->CNT - start; // 同时核对最后一位，避免没有下一位时遗漏超时。
        if (elapsed >= BSP_FLAME_WRITE_DWELL_US) {
            return (elapsed <= BSP_FLAME_WRITE_MAX_US) ? DEV_FLAME_OK : DEV_FLAME_TIMEOUT;
        }
    } while (--remaining != 0);
    return DEV_FLAME_TIMEOUT;
}

/** @brief 为 DOCI 建立低/高短脉冲，释放后等待至少 1us 再采样。
 * @param pins 本路引脚定义。
 * @param bit 接收本位数据；调用方保证非空并屏蔽帧内中断。
 * @return OK 或计数停转 TIMEOUT。 */
__STATIC_FORCEINLINE DevFlameResult bsp_flame_read_bit(const BspFlamePins *pins, bool *bit)
{
    GPIO_TypeDef *gpio = pins->gpio; // 预载 GPIO 地址和模式，脉冲间不做完整模式运算。
    uint32_t input_mode = gpio->MODER & ~pins->doci_field; // 保留本路以外的全部模式。
    uint32_t output_mode = input_mode | pins->doci_mode; // DOCI 临时输出模式。
    uint32_t high = pins->doci; // 时钟高电平命令。
    uint32_t low = high << BSP_FLAME_BSRR_RESET_SHIFT; // 时钟低电平命令。
    uint32_t start = TIM2->CNT; // 本位计时起点。
    uint32_t remaining = BSP_FLAME_BIT_POLL_LIMIT; // 停转时退出保护。
    gpio->BSRR = low;
    gpio->MODER = output_mode;
    __NOP(); __NOP(); __NOP(); __NOP();
    gpio->BSRR = high;
    __NOP(); __NOP(); __NOP(); __NOP();
    gpio->MODER = input_mode;
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); // 释放后 10 NOP=1.25us，再读 IDR。
    *bit = (gpio->IDR & high) != BSP_FLAME_FLAG_ABSENT;
    do {
        uint32_t elapsed = TIM2->CNT - start; // 最后一位也需满足手册的读位上限。
        if (elapsed >= BSP_FLAME_READ_DWELL_US) {
            return (elapsed <= BSP_FLAME_READ_MAX_US) ? DEV_FLAME_OK : DEV_FLAME_TIMEOUT;
        }
    } while (--remaining != 0);
    return DEV_FLAME_TIMEOUT;
}

/** @brief 按正式表仅配置本路；空闲 SERIN 低、DOCI 无上下拉高阻输入。
 * @param channel 合法逻辑通道。
 * @return OK 或参数、占用、板级、计时错误。 */
DevFlameResult BSP_Flame_Init(DevFlameChannel channel)
{
    const BspFlamePins *pins = bsp_flame_get_pins(channel); // 校验通道后才访问硬件。
    uint32_t saved_primask; // 保存调用前中断状态。
    uint32_t owned; // 两个所属 GPIO 的单比特总掩码。
    DevFlameResult result; // 保存首个错误。
    if (pins == NULL) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    result = bsp_flame_lock(&saved_primask);
    if (result != DEV_FLAME_OK) {
        return result;
    }
    result = bsp_flame_time_init();
    if (result != DEV_FLAME_OK) {
        bsp_flame_unlock(saved_primask);
        return result;
    }
    s_flame_initialized[channel] = false;
    owned = pins->serin | pins->doci;
    RCC->AHBENR |= pins->clock;
    (void)RCC->AHBENR;
    if (((RCC->AHBENR & pins->clock) != pins->clock) ||
        ((pins->gpio->LCKR & owned) != BSP_FLAME_FLAG_ABSENT)) {
        bsp_flame_unlock(saved_primask);
        return DEV_FLAME_IO_ERROR;
    }
    pins->gpio->BSRR = owned << BSP_FLAME_BSRR_RESET_SHIFT; // 启用输出前先预置低电平。
    pins->gpio->OTYPER &= ~owned; // 普通输出使用推挽，DOCI 只在协议指定阶段驱动。
    pins->gpio->OSPEEDR = (pins->gpio->OSPEEDR & ~pins->fields) | pins->speed;
    pins->gpio->PUPDR &= ~pins->fields; // 无内部上下拉，避免影响探头双向数据。
    pins->gpio->MODER = (pins->gpio->MODER & ~pins->fields) | pins->serin_mode;
    if (((pins->gpio->MODER & pins->fields) != pins->serin_mode) ||
        ((pins->gpio->OTYPER & owned) != BSP_FLAME_FLAG_ABSENT) ||
        ((pins->gpio->PUPDR & pins->fields) != BSP_FLAME_REGISTER_CLEAR) ||
        ((pins->gpio->OSPEEDR & pins->fields) != pins->speed)) {
        bsp_flame_serin_set(pins, false);
        bsp_flame_doci_release(pins);
        result = DEV_FLAME_IO_ERROR;
    } else {
        s_flame_initialized[channel] = true;
    }
    bsp_flame_unlock(saved_primask);
    return result;
}

/** @brief 同步写位流；帧内屏蔽中断，前后锁存等待恢复调用前中断状态。
 * @param channel 已初始化通道。
 * @param bits 右对齐位流。
 * @param bit_count 容器内的实际位数。
 * @return OK 或具名错误，任何传输失败均恢复所属信号空闲。 */
DevFlameResult BSP_Flame_WriteBits(DevFlameChannel channel, uint32_t bits, uint8_t bit_count)
{
    const BspFlamePins *pins = bsp_flame_get_pins(channel); // 本路引脚。
    uint32_t saved_primask; // 原中断状态。
    uint32_t previous = 0; // 前一位起点，第一位不检查间隔。
    uint32_t index; // 已发送位数。
    DevFlameResult result; // 保存传输主错误，清理不覆盖。
    DevFlameResult cleanup; // 低电平锁存等待结果。
    if ((pins == NULL) || (bit_count < BSP_FLAME_MIN_BITS) || (bit_count > BSP_FLAME_WRITE_CAPACITY) ||
        ((bit_count < BSP_FLAME_WRITE_CAPACITY) && ((bits >> bit_count) != BSP_FLAME_FLAG_ABSENT))) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    if (!s_flame_initialized[channel]) {
        return DEV_FLAME_NOT_READY;
    }
    result = bsp_flame_lock(&saved_primask);
    if (result != DEV_FLAME_OK) {
        return result;
    }
    result = bsp_flame_time_init();
    bsp_flame_serin_set(pins, false);
    bsp_flame_doci_release(pins);
    __set_PRIMASK(saved_primask);
    if (result == DEV_FLAME_OK) {
        result = bsp_flame_wait_us(BSP_FLAME_LATCH_US);
    }
    __disable_irq();
    if (result == DEV_FLAME_OK) {
        for (index = 0; index < bit_count; ++index) {
            uint32_t start = TIM2->CNT; // 同一位置的位间隔计数，包含外层循环开销。
            uint32_t elapsed = start - previous; // 无符号差允许计数回绕。
            bool value = ((bits >> (bit_count - index - 1U)) & BSP_FLAME_DATA_BIT) != BSP_FLAME_FLAG_ABSENT; // MSB 先出。
            if ((index != 0) && ((elapsed < BSP_FLAME_WRITE_MIN_US) ||
                                  (elapsed > BSP_FLAME_WRITE_MAX_US))) {
                result = DEV_FLAME_TIMEOUT;
                break;
            }
            previous = start;
            result = bsp_flame_write_bit(pins, value);
            if (result != DEV_FLAME_OK) {
                break;
            }
        }
    }
    bsp_flame_serin_set(pins, false);
    bsp_flame_doci_release(pins);
    __set_PRIMASK(saved_primask);
    cleanup = bsp_flame_wait_us(BSP_FLAME_LATCH_US);
    if (result == DEV_FLAME_OK) {
        result = cleanup;
    }
    bsp_flame_unlock(saved_primask);
    return result;
}

/** @brief 读取完整指定长度；只在成功并完成更新等待后提交位流。
 * @param channel 已初始化通道。
 * @param force true 强制建立高电平读取，false 仅在 DOCI 高时读取。
 * @param bit_count 实际帧长。
 * @param bits 成功时接收右对齐原始帧。
 * @return OK/PENDING 或具名错误。 */
DevFlameResult BSP_Flame_ReadBits(DevFlameChannel channel, bool force,
                                uint8_t bit_count, uint64_t *bits)
{
    const BspFlamePins *pins = bsp_flame_get_pins(channel); // 本路引脚。
    uint32_t saved_primask; // 原中断状态。
    uint32_t previous = 0; // 前一位起点。
    uint32_t index; // 已读取位数。
    uint64_t frame = 0; // 临时帧；失败不暴露残缺数据。
    DevFlameResult result; // 保存首个读取错误。
    DevFlameResult cleanup; // 读后更新等待结果。
    if ((pins == NULL) || (bits == NULL) || (bit_count < BSP_FLAME_MIN_BITS) ||
        (bit_count > BSP_FLAME_READ_CAPACITY)) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    if (!s_flame_initialized[channel]) {
        return DEV_FLAME_NOT_READY;
    }
    result = bsp_flame_lock(&saved_primask);
    if (result != DEV_FLAME_OK) {
        return result;
    }
    result = bsp_flame_time_init();
    if (result != DEV_FLAME_OK) {
        bsp_flame_serin_set(pins, false);
        bsp_flame_doci_release(pins);
        bsp_flame_unlock(saved_primask);
        return result;
    }
    if (!force && !bsp_flame_doci_level(pins)) {
        bsp_flame_unlock(saved_primask);
        return DEV_FLAME_PENDING;
    }
    if (force) {
        bsp_flame_doci_drive(pins, true);
    }
    __set_PRIMASK(saved_primask);
    result = bsp_flame_wait_us(BSP_FLAME_FORCE_US); // 外部事件或强制高电平均超过四周期。
    __disable_irq();
    if (result == DEV_FLAME_OK) {
        for (index = 0; index < bit_count; ++index) {
            uint32_t start = TIM2->CNT; // 起点间隔包含移位、采样与外层开销。
            bool value; // 本位临时样本。
            if ((index != 0) && ((uint32_t)(start - previous) > BSP_FLAME_READ_MAX_US)) {
                result = DEV_FLAME_TIMEOUT;
                break;
            }
            previous = start;
            result = bsp_flame_read_bit(pins, &value);
            if (result != DEV_FLAME_OK) {
                break;
            }
            frame = (frame << 1U) | (uint64_t)value;
        }
    }
    bsp_flame_doci_drive(pins, false);
    bsp_flame_serin_set(pins, false);
    __set_PRIMASK(saved_primask);
    cleanup = bsp_flame_wait_us(BSP_FLAME_UPDATE_US);
    __disable_irq();
    bsp_flame_doci_release(pins);
    if (result == DEV_FLAME_OK) {
        result = cleanup;
    }
    if (result == DEV_FLAME_OK) {
        *bits = frame;
    }
    bsp_flame_unlock(saved_primask);
    return result;
}

/** @brief 查询物理 DOCI 状态，不消费事件，不将高电平解释为火警。
 * @param channel 已初始化通道。
 * @param pending 成功时接收物理状态。
 * @return OK 或参数、未初始化、占用、板级错误。 */
DevFlameResult BSP_Flame_Pending(DevFlameChannel channel, bool *pending)
{
    const BspFlamePins *pins = bsp_flame_get_pins(channel); // 本路定义。
    uint32_t saved_primask; // 保护使用权与引脚查询。
    DevFlameResult result; // 计时及板级前提的核对结果。
    if ((pins == NULL) || (pending == NULL)) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    if (!s_flame_initialized[channel]) {
        return DEV_FLAME_NOT_READY;
    }
    result = bsp_flame_lock(&saved_primask);
    if (result != DEV_FLAME_OK) {
        return result;
    }
    result = bsp_flame_time_init();
    if (result == DEV_FLAME_OK) {
        *pending = bsp_flame_doci_level(pins);
    }
    bsp_flame_unlock(saved_primask);
    return result;
}

/** @brief 将 DOCI 拉低至少四个探头时钟周期，再恢复高阻输入。
 * @param channel 已初始化通道。
 * @return OK 或参数、状态、占用、计时错误；错误仍释放引脚。 */
DevFlameResult BSP_Flame_Clear(DevFlameChannel channel)
{
    const BspFlamePins *pins = bsp_flame_get_pins(channel); // 本路定义。
    uint32_t saved_primask; // 原中断状态。
    DevFlameResult result; // 操作结果。
    if (pins == NULL) {
        return DEV_FLAME_ARGUMENT_ERROR;
    }
    if (!s_flame_initialized[channel]) {
        return DEV_FLAME_NOT_READY;
    }
    result = bsp_flame_lock(&saved_primask);
    if (result != DEV_FLAME_OK) {
        return result;
    }
    result = bsp_flame_time_init();
    if (result == DEV_FLAME_OK) {
        bsp_flame_doci_drive(pins, false);
        __set_PRIMASK(saved_primask);
        result = bsp_flame_wait_us(BSP_FLAME_FORCE_US);
        __disable_irq();
    }
    bsp_flame_serin_set(pins, false);
    bsp_flame_doci_release(pins);
    bsp_flame_unlock(saved_primask);
    return result;
}
