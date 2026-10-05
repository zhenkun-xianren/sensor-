/* USART1 板级适配：RM0008 Rev21 第171-174、791-798、818-825页；
 * UMW ADM3485EARZ Apr2025 第2、6、7页；网表2026-09-18。
 * FreeRTOS 仅用于让出等待收发结果的任务，不在本层实现业务周期。 */
#include "bsp_rs485.h"
#include "bsp_byte_ring.h"
#include "bsp_time.h"
#include "stm32f103xb.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>

#define BSP_RS485_RX_ERROR_MASK (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)
static BspByteRing s_rx; // 中断生产、通信任务消费的接收字节缓冲区。
static volatile uint32_t s_rx_cycles[BSP_BYTE_RING_SIZE]; // 每个接收字节的 DWT 时间戳。
static volatile uint32_t s_last_activity; // 最近收发活动的周期计数，用于总线静默检查。
static volatile uint32_t s_tx_finished_ms; // 最后停止位发送完成时的毫秒计数。
static volatile uint8_t s_rx_error; // 串口错误或缓冲区溢出锁存。
static uint8_t s_initialized; // USART1 与方向脚是否已准备好。
static uint8_t s_tx_data[BSP_RS485_TX_CAPACITY]; // 中断发送使用的静态帧副本。
static volatile uint8_t s_tx_length; // 当前请求帧的字节总数。
static volatile uint8_t s_tx_position; // 下一个待写入 USART_DR 的字节位置。
static volatile uint8_t s_tx_done; // 最后停止位已发送并已切回接收的标志。
static volatile uint8_t s_tx_active; // 当前发送事务是否尚未结束。

/**
 * @brief 配置 GPIOA 高八位中单个引脚，保留其他引脚模式。
 * @param pin PA8 至 PA15 的引脚号。
 * @param mode STM32F1 的四位 GPIO 模式字段。
 */
static void rs485_configure_pin(uint32_t pin, uint32_t mode)
{
    uint32_t shift = (pin - BSP_RS485_GPIO_CRH_FIRST_PIN) * BSP_RS485_GPIO_FIELD_BITS; // CRH 字段偏移。
    GPIOA->CRH = (GPIOA->CRH & ~(BSP_RS485_GPIO_FIELD_MASK << shift)) | (mode << shift);
}

/**
 * @brief 设置共同连接 DE 和 /RE 的 PA8。
 * @param transmit 非零发送，零接收。
 */
static void rs485_set_direction(uint8_t transmit)
{
    GPIOA->BSRR = transmit != 0U ? BSP_RS485_DIR_MASK :
        BSP_RS485_DIR_MASK << BSP_RS485_BSRR_RESET_SHIFT;
}

/**
 * @brief 在短临界区中清空接收队列及硬件错误。
 */
static void rs485_clear_rx(void)
{
    uint32_t primask = __get_PRIMASK(); // 调用前的全局中断屏蔽状态。
    __disable_irq();
    BSP_ByteRing_Init(&s_rx);
    s_rx_error = 0U;
    /* SR 后读 DR 清除 RXNE/ORE/FE/NE，不能对特殊状态位普通读改写。 */
    (void)USART1->SR;
    (void)USART1->DR;
    __set_PRIMASK(primask);
}

/**
 * @brief 终止发送中断并释放总线，保留 USART 接收能力。
 */
static void rs485_abort_send(void)
{
    uint32_t primask = __get_PRIMASK(); // 进入临界区前的中断状态。
    __disable_irq();
    USART1->CR1 &= ~(USART_CR1_TXEIE | USART_CR1_TCIE);
    rs485_set_direction(0U);
    USART1->CR1 |= USART_CR1_RE | USART_CR1_RXNEIE;
    s_tx_active = 0U;
    s_last_activity = DWT->CYCCNT;
    __set_PRIMASK(primask);
}

/**
 * @brief 在短临界区内检查接收队列及硬件待接收状态，避免 ISR 到达时提前结束帧。
 * @param previousCycles 本帧最近字符完成的时间戳。
 * @return 队列和硬件均无新字节且静默满足结束余量时返回 1，否则返回 0。
 */
static uint8_t rs485_frame_has_ended(uint32_t previousCycles)
{
    uint32_t primask = __get_PRIMASK(); // 保存短临界区前的中断状态。
    uint8_t ended; // 静默边界和空队列共同决定的结束结果。
    __disable_irq();
    ended = (s_rx.tail == s_rx.head && s_rx_error == 0U &&
        (USART1->SR & (USART_SR_RXNE | BSP_RS485_RX_ERROR_MASK)) == 0U &&
        (uint32_t)(DWT->CYCCNT - previousCycles) >= BSP_RS485_FRAME_END_CYCLES) ? 1U : 0U;
    __set_PRIMASK(primask);
    return ended;
}

/**
 * @brief 初始化 USART1 为用户指定的 9600/8N1 和共同方向控制。
 * @return 成功返回 OK，时钟、DWT 或任务上下文无效返回 INVALID。
 */
BspRs485Status BSP_Rs485_Init(void)
{
    uint32_t primask; // 配置共享 AFIO 和 GPIO 寄存器时保存的中断状态。
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING ||
        SystemCoreClock != BSP_BOARD_HCLK_HZ ||
        (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        return BSP_RS485_INVALID;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    s_initialized = 0U;
    NVIC_DisableIRQ(USART1_IRQn);
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN | RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    /* SWJ_CFG 为只写字段，显式保留工程使用的 SWD，并保留 OLED SPI1 重映射。 */
    AFIO->MAPR = (AFIO->MAPR & ~(AFIO_MAPR_USART1_REMAP | AFIO_MAPR_SWJ_CFG)) |
        AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
    rs485_set_direction(0U);
    /* /RE 禁用时 RO 为高阻：PA10 内部上拉避免方向切换期间误检起始位。 */
    GPIOA->BSRR = BSP_RS485_RX_MASK;
    rs485_configure_pin(BSP_RS485_DIR_PIN, BSP_RS485_GPIO_OUT_PP_2MHZ);
    rs485_configure_pin(BSP_RS485_TX_PIN, BSP_RS485_GPIO_AF_PP_2MHZ);
    rs485_configure_pin(BSP_RS485_RX_PIN, BSP_RS485_GPIO_INPUT_PULL);
    USART1->CR1 = 0U;
    USART1->CR2 = 0U; /* STOP=00 为一停止位；不启用同步时钟输出。 */
    USART1->CR3 = 0U; /* RS485 双线接收/发送，不是 USART 单线 HDSEL 模式。 */
    USART1->BRR = BSP_RS485_UART_BRR_VALUE;
    BSP_ByteRing_Init(&s_rx);
    s_rx_error = 0U;
    s_tx_active = 0U;
    s_tx_done = 0U;
    s_last_activity = DWT->CYCCNT;
    (void)USART1->SR;
    (void)USART1->DR;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_SetPriority(USART1_IRQn, BSP_RS485_IRQ_PRIORITY);
    NVIC_ClearPendingIRQ(USART1_IRQn);
    s_initialized = 1U;
    NVIC_EnableIRQ(USART1_IRQn);
    __set_PRIMASK(primask);
    return BSP_RS485_OK;
}

/**
 * @brief 等静默后启动中断发送，在统一发送期限内阻塞等待 TC。
 * @param data 请求帧字节。
 * @param length 请求帧长度，范围 1 至静态发送容量。
 * @return 成功返回 OK，超时返回 TIMEOUT，无效参数或环境返回 INVALID。
 */
BspRs485Status BSP_Rs485_Send(const uint8_t *data, uint8_t length)
{
    uint32_t startMs; // 总线静默和发送共同使用的毫秒起点。
    uint32_t startCycles; // 发送器使能等待的 DWT 起点。
    uint32_t guard = BSP_RS485_SETUP_GUARD; // DWT 停滞时限制短等待循环。
    uint32_t primask; // 启动中断发送前的中断状态。
    uint8_t index; // 复制请求帧时的字节下标。
    if (s_initialized == 0U || data == NULL || length == 0U ||
        length > BSP_RS485_TX_CAPACITY || s_tx_active != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return BSP_RS485_INVALID;
    }
    startMs = BSP_Time_GetMs();
    /* 额外保留一字符余量，使尚在移位接收中的字节能先进入 RXNE 再判断静默。 */
    while ((uint32_t)(DWT->CYCCNT - s_last_activity) < BSP_RS485_FRAME_END_CYCLES) {
        if ((uint32_t)(BSP_Time_GetMs() - startMs) >= BSP_RS485_TX_TIMEOUT_MS) {
            rs485_abort_send();
            return BSP_RS485_TIMEOUT;
        }
        vTaskDelay(1U);
    }
    primask = __get_PRIMASK();
    __disable_irq();
    rs485_clear_rx();
    USART1->CR1 &= ~(USART_CR1_RE | USART_CR1_RXNEIE);
    for (index = 0U; index < length; ++index) {
        s_tx_data[index] = data[index];
    }
    s_tx_length = length;
    s_tx_position = 0U;
    s_tx_done = 0U;
    s_tx_active = 1U;
    rs485_set_direction(1U);
    startCycles = DWT->CYCCNT;
    __set_PRIMASK(primask);
    /* UMW 最大使能延迟 900 ns，选择有界 1 us，避免首个起始位被截断。 */
    while ((uint32_t)(DWT->CYCCNT - startCycles) < BSP_RS485_SETUP_CYCLES) {
        if (guard == 0U) {
            rs485_abort_send();
            return BSP_RS485_INVALID;
        }
        --guard;
    }
    USART1->CR1 |= USART_CR1_TXEIE;
    while (s_tx_done == 0U) {
        if ((uint32_t)(BSP_Time_GetMs() - startMs) >= BSP_RS485_TX_TIMEOUT_MS) {
            rs485_abort_send();
            return BSP_RS485_TIMEOUT;
        }
        vTaskDelay(1U);
    }
    return BSP_RS485_OK;
}

/**
 * @brief 按时间戳和静默边界接收一帧，等待时阻塞让出 CPU。
 * @param data 接收帧的输出缓冲区。
 * @param capacity 接收容量。
 * @param length 已接收长度输出地址。
 * @param timeoutMs 自请求 TC 完成后的整体毫秒期限。
 * @return 成功返回 OK，超时、硬件错误、帧错误或参数错误返回对应状态。
 */
BspRs485Status BSP_Rs485_ReadFrame(uint8_t *data, uint8_t capacity,
    uint8_t *length, uint32_t timeoutMs)
{
    uint32_t previousCycles = 0U; // 当前帧上一个字符完成的周期计数。
    uint32_t currentCycles; // 队列中下一个字符的完成时间。
    uint32_t difference; // 相邻字符完成时刻之差。
    uint8_t byte; // 本次从环形队列读取的字节。
    uint8_t tail; // 读取字节前对应的时间戳索引。
    if (s_initialized == 0U || data == NULL || length == NULL ||
        capacity == 0U || timeoutMs == 0U || s_tx_active != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return BSP_RS485_INVALID;
    }
    *length = 0U;
    for (;;) {
        if ((uint32_t)(BSP_Time_GetMs() - s_tx_finished_ms) >= timeoutMs) {
            return BSP_RS485_TIMEOUT;
        }
        if (s_rx_error != 0U) {
            return BSP_RS485_RX_ERROR;
        }
        if (s_rx.tail != s_rx.head) {
            tail = s_rx.tail;
            currentCycles = s_rx_cycles[tail];
            if (*length != 0U) {
                difference = (uint32_t)(currentCycles - previousCycles);
                if (difference >= BSP_RS485_FRAME_END_CYCLES) {
                    return BSP_RS485_OK; /* 下一独立帧保留在队列中。 */
                }
                /* RXNE 时间戳间包含一个字符时间，扣除此项后才是线上静默。 */
                if (difference > BSP_RS485_CHAR_CYCLES + BSP_RS485_T1_5_CYCLES) {
                    return BSP_RS485_FRAME_ERROR;
                }
            }
            if (*length >= capacity) {
                return BSP_RS485_FRAME_ERROR; /* 不接受正确回显后紧跟同帧多余字节。 */
            }
            if (BSP_ByteRing_Pop(&s_rx, &byte) != 0U) {
                data[*length] = byte;
                ++(*length);
                previousCycles = currentCycles;
            }
        } else {
            if (*length != 0U && rs485_frame_has_ended(previousCycles) != 0U) {
                return BSP_RS485_OK;
            }
            vTaskDelay(1U);
        }
    }
}

/**
 * @brief 保存接收字节及时间戳，连续供给发送字节，TC 后立即释放总线。
 */
void BSP_Rs485_IrqHandler(void)
{
    uint32_t status = USART1->SR; // 一次读取的串口状态，须在 DR 之前读取。
    uint32_t cycles; // 当前接收完成事件的周期计数。
    uint8_t byte; // 从 DR 读取的八位数据。
    uint8_t head; // 发布字节前的生产者索引。
    if ((status & (USART_SR_RXNE | BSP_RS485_RX_ERROR_MASK)) != 0U) {
        byte = (uint8_t)USART1->DR;
        cycles = DWT->CYCCNT;
        s_last_activity = cycles;
        if ((status & BSP_RS485_RX_ERROR_MASK) != 0U) {
            s_rx_error = 1U;
        } else if (s_tx_active == 0U) {
            head = s_rx.head;
            s_rx_cycles[head] = cycles; /* 先保存时间戳，再由 Push 发布字节。 */
            if (BSP_ByteRing_Push(&s_rx, byte) == 0U) {
                s_rx_error = 1U;
            }
        }
    }
    if ((USART1->CR1 & USART_CR1_TXEIE) != 0U &&
        (USART1->SR & USART_SR_TXE) != 0U) {
        /* 读 SR 后写 DR 清除 TC；TXE 中断连续提供字节，不受业务任务调度影响。 */
        (void)USART1->SR;
        USART1->DR = s_tx_data[s_tx_position];
        ++s_tx_position;
        if (s_tx_position >= s_tx_length) {
            USART1->CR1 = (USART1->CR1 & ~USART_CR1_TXEIE) | USART_CR1_TCIE;
        }
    }
    /* 重新读状态，不能使用写 DR 前的旧 TC 快照提前释放最后停止位。 */
    if ((USART1->CR1 & USART_CR1_TCIE) != 0U &&
        (USART1->SR & USART_SR_TC) != 0U) {
        USART1->CR1 &= ~USART_CR1_TCIE;
        rs485_set_direction(0U);
        s_last_activity = DWT->CYCCNT;
        s_tx_finished_ms = BSP_Time_GetMs();
        USART1->CR1 |= USART_CR1_RE | USART_CR1_RXNEIE;
        s_tx_active = 0U;
        s_tx_done = 1U;
    }
}

/**
 * @brief 将启动向量表中的 USART1 中断接入板级收发处理。
 */
void USART1_IRQHandler(void)
{
    BSP_Rs485_IrqHandler();
}

