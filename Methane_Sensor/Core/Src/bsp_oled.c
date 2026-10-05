#include "bsp_oled.h"
#include "bsp_board.h"
#include "stm32f103xb.h"
#include <stddef.h>

#define BSP_OLED_GPIO_FIELD_BITS       4U
#define BSP_OLED_GPIO_FIELD_MASK     0xFUL
#define BSP_OLED_GPIO_AF_PP_50MHZ    0xBUL
#define BSP_OLED_GPIO_OUT_PP_2MHZ    0x2UL
#define BSP_OLED_SPI_BAUD_BITS       SPI_CR1_BR_2
#define BSP_OLED_DWT_GUARD_FACTOR       8U

/* 手册第 11 页给出的最小时钟周期为 100 ns；当前 APB2 /32 = 2 MHz。 */
#if (BSP_BOARD_PCLK2_HZ / BSP_OLED_SPI_CLOCK_DIVISOR) > BSP_OLED_SPI_MAX_HZ
#error "OLED SPI clock exceeds the module timing limit"
#endif

static uint8_t s_oled_bus_ready; // SPI 总线和 OLED 控制脚是否已完成初始化。
volatile BspOledError g_bsp_oled_error; // 本次初始化后首次出现的板级错误。
volatile uint32_t g_bsp_oled_bytes_sent; // SPI 已完成的字节数，便于区分总线停滞和持续发送。
volatile uint32_t g_bsp_oled_debug_cr_before; // 清除跟踪引脚分配前的 DBGMCU 配置。
volatile uint32_t g_bsp_oled_sr_on_error; // 首次错误现场的 SPI 状态。

/**
 * @brief 保存首次错误及其 SPI 状态，避免后续清理覆盖故障现场。
 * @param error 本次检测到的板级错误。
 */
static void oled_record_error(BspOledError error)
{
    if (g_bsp_oled_error == BSP_OLED_ERROR_NONE) {
        g_bsp_oled_sr_on_error = SPI1->SR;
        g_bsp_oled_error = error;
    }
}

/**
 * @brief 为指定 PB 引脚配置 STM32F1 四位 GPIO 模式字段。
 * @param pin GPIOB 引脚编号。
 * @param mode 目标引脚的 STM32F1 GPIO 模式字段。
 */
static void oled_configure_pin(uint32_t pin, uint32_t mode)
{
    volatile uint32_t *reg = pin < 8U ? &GPIOB->CRL : &GPIOB->CRH; // 引脚对应的 GPIO 配置寄存器。
    uint32_t shift = (pin % 8U) * BSP_OLED_GPIO_FIELD_BITS; // 引脚模式字段在寄存器中的位偏移。

    *reg = (*reg & ~(BSP_OLED_GPIO_FIELD_MASK << shift)) | (mode << shift);
}

/**
 * @brief 使用内核周期计数器执行有轮询上限的复位等待。
 * @param milliseconds 需要等待的毫秒数。
 * @return 达到指定等待时间返回 1，计数器未运行返回 0。
 */
static uint8_t oled_delay_ms(uint32_t milliseconds)
{
    uint32_t cycles = (BSP_BOARD_HCLK_HZ / 1000UL) * milliseconds; // 本次等待对应的 CPU 周期数。
    uint32_t start = DWT->CYCCNT; // 等待开始时的周期计数快照。
    uint32_t guard = cycles * BSP_OLED_DWT_GUARD_FACTOR; // 周期计数器停滞时防止无限循环的上限。

    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
        if (guard == 0U) {
            return 0U;
        }
        --guard;
    }
    return 1U;
}

/**
 * @brief 在有限次数内等待 SPI 状态位达到指定状态。
 * @param mask 待检查的 SPI 状态位掩码。
 * @param is_set 非零表示等待置位，零表示等待清零。
 * @return 状态满足要求返回 1，超时返回 0。
 */
static uint8_t oled_wait_spi(uint32_t mask, uint8_t is_set)
{
    uint32_t remaining = BSP_OLED_SPI_WAIT_LIMIT; // 等待 SPI 状态变化的剩余轮询次数。

    while (((SPI1->SR & mask) != 0U) != (is_set != 0U)) {
        if (remaining == 0U) {
            return 0U;
        }
        --remaining;
    }
    return 1U;
}

/**
 * @brief 开启 GPIO、AFIO、SPI1 时钟并将 SPI1 重映射到 PB3/PB5。
 * @return 时钟、引脚、SPI 和 OLED 复位均就绪返回 1，否则返回 0。
 */
uint8_t BSP_Oled_Init(void)
{
    uint32_t control_mask = (1UL << BSP_OLED_RESET_PIN) |
                            (1UL << BSP_OLED_DC_PIN) |
                            (1UL << BSP_OLED_CS1_PIN); // OLED 复位、命令选择和片选控制脚掩码。

    s_oled_bus_ready = 0U;
    g_bsp_oled_error = BSP_OLED_ERROR_NONE;
    g_bsp_oled_bytes_sent = 0U;
    g_bsp_oled_sr_on_error = 0U;
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPBEN |
                    RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;

    /* RM0008 的 TRACE 引脚分配包含 PB3/SWO；关闭引脚跟踪以释放 SPI 时钟。
       内核 DWT 计时仍可使用，PA13/PA14 的 SWD 不受 TRACE_IOEN 影响。 */
    g_bsp_oled_debug_cr_before = DBGMCU->CR;
    DBGMCU->CR &= ~DBGMCU_CR_TRACE_IOEN;

    /* SPI1 重映射释放 PB3/PB5；仅禁用 JTAG，保留 PA13/PA14 的 SWD。 */
    AFIO->MAPR = (AFIO->MAPR & ~(AFIO_MAPR_SPI1_REMAP | AFIO_MAPR_SWJ_CFG)) |
                 AFIO_MAPR_SPI1_REMAP | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;

    /* 切换输出模式前先置 CS1/RES/DC 为高，避免意外选中显示芯片。 */
    GPIOB->BSRR = control_mask;
    oled_configure_pin(BSP_OLED_SPI_SCK_PIN, BSP_OLED_GPIO_AF_PP_50MHZ);
    oled_configure_pin(BSP_OLED_SPI_MOSI_PIN, BSP_OLED_GPIO_AF_PP_50MHZ);
    oled_configure_pin(BSP_OLED_RESET_PIN, BSP_OLED_GPIO_OUT_PP_2MHZ);
    oled_configure_pin(BSP_OLED_DC_PIN, BSP_OLED_GPIO_OUT_PP_2MHZ);
    oled_configure_pin(BSP_OLED_CS1_PIN, BSP_OLED_GPIO_OUT_PP_2MHZ);

    /* 手册第 12 页显示时钟空闲高、上升沿采样，故使用 SPI 模式 3。 */
    SPI1->CR1 = 0U;
    SPI1->CR2 = 0U;
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
                SPI_CR1_CPOL | SPI_CR1_CPHA | BSP_OLED_SPI_BAUD_BITS;
    SPI1->CR1 |= SPI_CR1_SPE;

    /* 手册第 16 页要求低电平复位 200 ms，再释放复位。 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    /* 与 RS485 共用周期计数，只使能，不清零正在运行的时间戳。 */
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    GPIOB->BRR = 1UL << BSP_OLED_RESET_PIN;
    if (oled_delay_ms(BSP_OLED_RESET_LOW_MS) == 0U) {
        oled_record_error(BSP_OLED_ERROR_RESET_LOW);
        GPIOB->BSRR = 1UL << BSP_OLED_RESET_PIN;
        return 0U;
    }
    GPIOB->BSRR = 1UL << BSP_OLED_RESET_PIN;
    if (oled_delay_ms(BSP_OLED_RESET_HIGH_MS) == 0U) {
        oled_record_error(BSP_OLED_ERROR_RESET_HIGH);
        return 0U;
    }
    s_oled_bus_ready = 1U;
    return 1U;
}

/**
 * @brief 在 CS1 有效期间通过 SPI1 发送完整命令或数据缓冲区。
 * @param bytes 待发送字节数组。
 * @param length 数组中的字节数，必须大于零。
 * @param is_data 非零表示显示数据，零表示控制命令。
 * @return 全部字节完成发送返回 1，参数无效或硬件超时返回 0。
 */
uint8_t BSP_Oled_Write(const uint8_t *bytes, uint16_t length, uint8_t is_data)
{
    uint16_t index; // 当前准备发送的数组字节索引。
    uint8_t ok = 1U; // 本次 SPI 传输是否仍然成功。
    volatile uint8_t *data_reg = (volatile uint8_t *)&SPI1->DR; // SPI1 数据寄存器的八位访问指针。

    if (s_oled_bus_ready == 0U || bytes == NULL || length == 0U) {
        oled_record_error(BSP_OLED_ERROR_ARGUMENT);
        return 0U;
    }
    GPIOB->BSRR = is_data != 0U ? (1UL << BSP_OLED_DC_PIN) :
                  (1UL << (BSP_OLED_DC_PIN + 16U));
    GPIOB->BRR = 1UL << BSP_OLED_CS1_PIN;
    for (index = 0U; index < length; ++index) {
        if (oled_wait_spi(SPI_SR_TXE, 1U) == 0U) {
            oled_record_error(BSP_OLED_ERROR_TXE);
            ok = 0U;
            break;
        }
        *data_reg = bytes[index];
        if (oled_wait_spi(SPI_SR_RXNE, 1U) == 0U) {
            oled_record_error(BSP_OLED_ERROR_RXNE);
            ok = 0U;
            break;
        }
        (void)*data_reg; /* 读取无用的 MISO 字节，防止接收溢出。 */
        ++g_bsp_oled_bytes_sent;
    }
    if (oled_wait_spi(SPI_SR_BSY, 0U) == 0U) {
        oled_record_error(BSP_OLED_ERROR_BSY);
        ok = 0U;
    }
    GPIOB->BSRR = 1UL << BSP_OLED_CS1_PIN;
    return ok;
}
