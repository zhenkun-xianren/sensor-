#include "bsp_adc.h"
#include "stm32f103xb.h"
#include <stdbool.h>
#include <stddef.h>

static bool s_adc_ready = false; // 保存最近一次ADC初始化是否成功。

/**
 * @brief 使用CPU周期精确等待ADC控制位清零，避免1ms刻度边界导致误超时。
 * @param flag ADC_CR2中需要等待硬件清零的位。
 * @param start_cycles 当前操作整体开始时的DWT周期计数。
 * @param timeout_cycles 整体允许等待的CPU周期数。
 * @return 标志清零返回BSP_ADC_OK，时间或轮询预算用尽返回BSP_ADC_TIMEOUT。
 */
static BspAdcStatus adc_wait_clear(uint32_t flag, uint32_t start_cycles, uint32_t timeout_cycles)
{
    uint32_t polls = BSP_ADC_WAIT_POLL_LIMIT; // 保存计时器异常时的备用轮询预算。
    while ((ADC1->CR2 & flag) != BSP_ADC_CLEAR_FLAGS) {
        if ((uint32_t)(DWT->CYCCNT - start_cycles) >= timeout_cycles || polls == 0UL) {
            /* 两次检查之间可能被抢占；控制位已清零时优先承认操作完成。 */
            return (ADC1->CR2 & flag) == BSP_ADC_CLEAR_FLAGS ? BSP_ADC_OK : BSP_ADC_TIMEOUT;
        }
        --polls;
    }
    return BSP_ADC_OK;
}

/**
 * @brief 执行单通道软件触发转换，读取DR清除EOC并返回本次结果。
 * @param channel 当前转换通道编号，调用者仅传本板已配置的通道6或7。
 * @param output 接收成功转换结果的非空指针。
 * @return 成功BSP_ADC_OK，转换等待超时BSP_ADC_TIMEOUT。
 */
static BspAdcStatus adc_read_channel(uint32_t channel, uint16_t *output)
{
    uint32_t start_cycles; // 保存本次转换的精确起始时间。
    uint32_t polls = BSP_ADC_WAIT_POLL_LIMIT; // 保存转换等待的备用轮询预算。
    uint32_t saved_mask; // 保存最终超时判定前的PRIMASK，避免复查与停止之间被切走。
    bool timed_out; // 保存短临界区内最终确认的硬件未完成状态。
    /* 每次仅配置一个规则通道，不使用无DMA的多通道扫描读取DR。 */
    ADC1->SQR3 = channel << ADC_SQR3_SQ1_Pos;
    ADC1->SR = BSP_ADC_CLEAR_FLAGS; // 清除上次状态，防止误取旧EOC结果。
    start_cycles = DWT->CYCCNT;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while ((ADC1->SR & ADC_SR_EOC) == BSP_ADC_CLEAR_FLAGS) {
        if ((uint32_t)(DWT->CYCCNT - start_cycles) >=
                BSP_ADC_CONVERSION_TIMEOUT_MS * BSP_ADC_CYCLES_PER_MS || polls == 0UL) {
            /* EOC初查与时间检查之间允许抢占；硬件可能已在后台完成转换。
               仅最终判定及停止使用短临界区，不屏蔽整个转换等待过程。 */
            saved_mask = __get_PRIMASK();
            __disable_irq();
            __DMB();
            timed_out = (ADC1->SR & ADC_SR_EOC) == BSP_ADC_CLEAR_FLAGS;
            if (timed_out) {
                ADC1->CR2 &= ~ADC_CR2_ADON; // 确认真正未完成后停止，下一周期重新初始化。
                s_adc_ready = false;
            }
            __DMB();
            __set_PRIMASK(saved_mask);
            if (timed_out) return BSP_ADC_TIMEOUT;
            break; // 已完成结果仍有效，不能把任务恢复较晚当成ADC硬件未完成。
        }
        --polls;
    }
    *output = (uint16_t)(ADC1->DR & BSP_ADC_RAW_MASK);
    return BSP_ADC_OK;
}

/**
 * @brief 保持板级8MHz时钟，配置两脚模拟输入及4MHz ADC1并进行上电校准。
 * @return BSP_ADC_OK成功，BSP_ADC_TIMEOUT校准超时，BSP_ADC_HARDWARE_ERROR时钟或时基异常。
 */
BspAdcStatus BSP_Adc_Init(void)
{
    uint32_t start_cycles; // 保存计数器探测、上电稳定及校准阶段的起始周期。
    uint32_t polls; // 保存有限稳定等待的预算。
    uint32_t probe; // 遍历计数器探测所需的短指令序列。
    BspAdcStatus result; // 保存校准各阶段的结果。
    s_adc_ready = false;
    if (BSP_Board_CheckClock() != BSP_BOARD_OK || BSP_ADC_CLOCK_HZ < BSP_ADC_CLOCK_MIN_HZ ||
        BSP_ADC_CLOCK_HZ > BSP_ADC_CLOCK_MAX_HZ) return BSP_ADC_HARDWARE_ERROR;

    /* DWT是Cortex-M3内核周期计数器，不占用TIM，不复位已有计数，也不改SWD配置。 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    start_cycles = DWT->CYCCNT;
    for (probe = 0UL; probe < BSP_ADC_COUNTER_PROBE_COUNT; ++probe) __NOP();
    if (DWT->CYCCNT == start_cycles) return BSP_ADC_HARDWARE_ERROR;

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_ADCPRE) | RCC_CFGR_ADCPRE_DIV2;
    /* 只修改PA6/PA7，保留串口、I2C、显示及SWD所属的其他GPIO字段。 */
    GPIOA->CRL &= ~(GPIO_CRL_MODE6 | GPIO_CRL_CNF6 | GPIO_CRL_MODE7 | GPIO_CRL_CNF7);
    RCC->APB2RSTR |= RCC_APB2RSTR_ADC1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_ADC1RST;
    ADC1->CR1 = BSP_ADC_RESET_CONFIGURATION; // 独立、单通道、轮询，不开扫描或ADC中断。
    ADC1->SQR1 = BSP_ADC_RESET_CONFIGURATION; // 规则组长度为1。
    ADC1->SQR2 = BSP_ADC_RESET_CONFIGURATION;
    ADC1->SQR3 = BSP_ADC_SIGNAL_CHANNEL << ADC_SQR3_SQ1_Pos;
    ADC1->SMPR2 = ADC_SMPR2_SMP6 | ADC_SMPR2_SMP7; // 两通道均为239.5周期。
    ADC1->CR2 = ADC_CR2_EXTSEL | ADC_CR2_EXTTRIG | ADC_CR2_ADON; // 软件触发、右对齐、不连续。
    __DSB();
    start_cycles = DWT->CYCCNT;
    polls = BSP_ADC_WAIT_POLL_LIMIT;
    while ((uint32_t)(DWT->CYCCNT - start_cycles) < BSP_ADC_POWER_STABLE_MS * BSP_ADC_CYCLES_PER_MS) {
        if (polls == 0UL) {
            ADC1->CR2 &= ~ADC_CR2_ADON;
            return BSP_ADC_HARDWARE_ERROR;
        }
        --polls;
    }
    start_cycles = DWT->CYCCNT;
    ADC1->CR2 |= ADC_CR2_RSTCAL;
    result = adc_wait_clear(ADC_CR2_RSTCAL, start_cycles,
        BSP_ADC_CALIBRATION_TIMEOUT_MS * BSP_ADC_CYCLES_PER_MS);
    if (result == BSP_ADC_OK) {
        ADC1->CR2 |= ADC_CR2_CAL;
        result = adc_wait_clear(ADC_CR2_CAL, start_cycles,
            BSP_ADC_CALIBRATION_TIMEOUT_MS * BSP_ADC_CYCLES_PER_MS);
    }
    if (result != BSP_ADC_OK) {
        ADC1->CR2 &= ~ADC_CR2_ADON;
        return result;
    }
    (void)ADC1->DR; // 校准结束后的DR存放校准码，不将其作为采样数据。
    ADC1->SR = BSP_ADC_CLEAR_FLAGS;
    if ((GPIOA->CRL & (GPIO_CRL_MODE6 | GPIO_CRL_CNF6 | GPIO_CRL_MODE7 | GPIO_CRL_CNF7)) !=
            BSP_ADC_CLEAR_FLAGS || (RCC->CFGR & RCC_CFGR_ADCPRE) != RCC_CFGR_ADCPRE_DIV2 ||
        (ADC1->CR2 & ADC_CR2_ADON) == BSP_ADC_CLEAR_FLAGS) {
        ADC1->CR2 &= ~ADC_CR2_ADON;
        return BSP_ADC_HARDWARE_ERROR;
    }
    s_adc_ready = true;
    return BSP_ADC_OK;
}

/**
 * @brief 以两个单通道转换取得信号及参考原始值，全部成功后提交结果。
 * @param signal_raw 测量通道的非空输出指针。
 * @param reference_raw 参考通道的非空输出指针，不得与signal_raw相同。
 * @return 成功BSP_ADC_OK，参数错误BSP_ADC_ARGUMENT_ERROR，未就绪BSP_ADC_NOT_READY，超时BSP_ADC_TIMEOUT。
 */
BspAdcStatus BSP_Adc_ReadPair(uint16_t *signal_raw, uint16_t *reference_raw)
{
    uint16_t signal; // 保存本次信号转换结果，全部成功后才提交。
    uint16_t reference; // 保存本次参考转换结果，防止失败时混入旧结果。
    BspAdcStatus result; // 保存每一步转换的返回状态。
    if (signal_raw == NULL || reference_raw == NULL || signal_raw == reference_raw)
        return BSP_ADC_ARGUMENT_ERROR;
    if (!s_adc_ready) return BSP_ADC_NOT_READY;
    if ((ADC1->CR2 & ADC_CR2_ADON) == BSP_ADC_CLEAR_FLAGS ||
        (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == BSP_ADC_CLEAR_FLAGS) {
        s_adc_ready = false;
        return BSP_ADC_NOT_READY;
    }
    result = adc_read_channel(BSP_ADC_SIGNAL_CHANNEL, &signal);
    if (result != BSP_ADC_OK) return result;
    result = adc_read_channel(BSP_ADC_REFERENCE_CHANNEL, &reference);
    if (result != BSP_ADC_OK) return result;
    *signal_raw = signal;
    *reference_raw = reference;
    return BSP_ADC_OK;
}
