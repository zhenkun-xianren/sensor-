#ifndef BSP_FLAME_H
#define BSP_FLAME_H

#include "dev_flame.h"
#include "bsp_board.h"

/* 根目录正式引脚表 V1.2；厂家脚号与工程自建焊盘编号不等同。
 * 顶面插装且 GND/Vdd 对应正确：DOCI=PA6/PB0/PB3，SERIN=PA7/PB1/PB4。 */
#define BSP_FLAME_U22_PORT          GPIOA
#define BSP_FLAME_U22_CLOCK         RCC_AHBENR_GPIOAEN
#define BSP_FLAME_U22_SERIN         GPIO_BSRR_BS_7
#define BSP_FLAME_U22_DOCI          GPIO_BSRR_BS_6
#define BSP_FLAME_U22_FIELDS        (GPIO_MODER_MODER6 | GPIO_MODER_MODER7)
#define BSP_FLAME_U22_SERIN_MODE    GPIO_MODER_MODER7_0
#define BSP_FLAME_U22_DOCI_FIELD    GPIO_MODER_MODER6
#define BSP_FLAME_U22_DOCI_MODE     GPIO_MODER_MODER6_0
#define BSP_FLAME_U22_SPEED         (GPIO_OSPEEDR_OSPEEDR6_0 | GPIO_OSPEEDR_OSPEEDR7_0)
#define BSP_FLAME_U23_PORT          GPIOB
#define BSP_FLAME_U23_CLOCK         RCC_AHBENR_GPIOBEN
#define BSP_FLAME_U23_SERIN         GPIO_BSRR_BS_1
#define BSP_FLAME_U23_DOCI          GPIO_BSRR_BS_0
#define BSP_FLAME_U23_FIELDS        (GPIO_MODER_MODER0 | GPIO_MODER_MODER1)
#define BSP_FLAME_U23_SERIN_MODE    GPIO_MODER_MODER1_0
#define BSP_FLAME_U23_DOCI_FIELD    GPIO_MODER_MODER0
#define BSP_FLAME_U23_DOCI_MODE     GPIO_MODER_MODER0_0
#define BSP_FLAME_U23_SPEED         (GPIO_OSPEEDR_OSPEEDR0_0 | GPIO_OSPEEDR_OSPEEDR1_0)
#define BSP_FLAME_U24_PORT          GPIOB
#define BSP_FLAME_U24_CLOCK         RCC_AHBENR_GPIOBEN
#define BSP_FLAME_U24_SERIN         GPIO_BSRR_BS_4
#define BSP_FLAME_U24_DOCI          GPIO_BSRR_BS_3
#define BSP_FLAME_U24_FIELDS        (GPIO_MODER_MODER3 | GPIO_MODER_MODER4)
#define BSP_FLAME_U24_SERIN_MODE    GPIO_MODER_MODER4_0
#define BSP_FLAME_U24_DOCI_FIELD    GPIO_MODER_MODER3
#define BSP_FLAME_U24_DOCI_MODE     GPIO_MODER_MODER3_0
#define BSP_FLAME_U24_SPEED         (GPIO_OSPEEDR_OSPEEDR3_0 | GPIO_OSPEEDR_OSPEEDR4_0)

/* RM0091：本适配仅接受板级 AHB/APB 不分频，故 TIM2 时钟=PCLK1。
 * NOP 短脉冲仅适配 HCLK=8MHz、Flash 零等待；变化时须重新核对编译指令。 */
#define BSP_FLAME_TIM2_HZ           BSP_BOARD_PCLK1_HZ
#define BSP_FLAME_PULSE_HCLK_HZ     8000000UL // 当前短脉冲的指令计时基准。
#define BSP_FLAME_COUNTER_HZ        1000000UL // TIM2 每微秒计数一次。
#define BSP_FLAME_COUNTER_MAX       UINT32_MAX // TIM2 32 位自然回绕。
#define BSP_FLAME_REGISTER_CLEAR    0UL // 专属定时器复位字段和普通 GPIO 输入字段。
#define BSP_FLAME_FLAG_ABSENT       0UL // 寄存器掩码无置位。
#define BSP_FLAME_THREAD_MODE       0UL // IPSR 的线程上下文编码。
#define BSP_FLAME_BSRR_RESET_SHIFT  16U // BSRR 高半部写 1 复位。
#define BSP_FLAME_WRITE_CAPACITY    32U // 写入容器位宽上限。
#define BSP_FLAME_READ_CAPACITY     40U // 此板级实现允许的最大读位数。
#define BSP_FLAME_MIN_BITS           1U // 位流不得为空。
#define BSP_FLAME_DATA_BIT          1UL // 单个位流数据掩码。
#define BSP_FLAME_WRITE_DWELL_US    70UL // 单位函数内部驻留，外层开销纳入 68～82us 预算。
#define BSP_FLAME_WRITE_MIN_US      68UL // 兼容最慢 FCLK 的写位间隔下界。
#define BSP_FLAME_WRITE_MAX_US      82UL // 留出 3/FCLK 在 36kHz 下的边界裕量。
#define BSP_FLAME_READ_DWELL_US     14UL // ARMCC5 指令核对后为外层模式预载/移位留出约 6～8us。
#define BSP_FLAME_READ_MAX_US       24UL // 手册读取位周期上限。
#define BSP_FLAME_LATCH_US          600UL // 大于 16/30kHz，写入前后保持低电平。
#define BSP_FLAME_FORCE_US          150UL // 大于 4/30kHz，强制读及清事件。
#define BSP_FLAME_UPDATE_US         1100UL // 大于 32/30kHz，读完低电平更新内部寄存器。
#define BSP_FLAME_WAIT_MAX_US       2000UL // 本驱动等待参数上限，防止乘法溢出。
#define BSP_FLAME_WAIT_MARGIN       64UL // 计数器停转时的有界轮询余量。
#define BSP_FLAME_BIT_POLL_LIMIT    1024UL // 屏蔽中断期间单个位的最大轮询次数。
#define BSP_FLAME_COUNTER_PROBE_US  3UL // 初始化检查计数器确实前进。

/** @brief 初始化本路所属 GPIO 和专属 TIM2；不得在 ISR 调用。
 * @param channel 三路合法通道之一。
 * @return OK 或参数、资源占用、板级、计时错误。 */
DevFlameResult BSP_Flame_Init(DevFlameChannel channel);
/** @brief SERIN 按高位先出写入位流，包含前后低电平锁存等待。
 * @param channel 已初始化的逻辑通道。
 * @param bits 要写入的右对齐位流，未使用高位须为零。
 * @param bit_count 1～32 位，协议层负责传入实际协议长度。
 * @return OK 或参数、状态、占用、计时错误；失败恢复空闲。 */
DevFlameResult BSP_Flame_WriteBits(DevFlameChannel channel, uint32_t bits, uint8_t bit_count);
/** @brief DOCI 读取右对齐位流，失败保持输出不变。
 * @param channel 已初始化的逻辑通道。
 * @param force 是否主动拉高建立强制读。
 * @param bit_count 1～40 位，完整协议帧由 Port 传入。
 * @param bits 成功时接收原始位流。
 * @return OK、PENDING 或参数、状态、占用、计时错误。 */
DevFlameResult BSP_Flame_ReadBits(DevFlameChannel channel, bool force,
                                uint8_t bit_count, uint64_t *bits);
/** @brief 读取 DOCI 电平；其含义取决于配置的中断源。
 * @param channel 已初始化的逻辑通道。
 * @param pending 成功时接收高电平状态，失败保持不变。
 * @return OK 或参数、状态、占用、板级错误。 */
DevFlameResult BSP_Flame_Pending(DevFlameChannel channel, bool *pending);
/** @brief 低电平保持至少四个探头时钟周期后释放 DOCI。
 * @param channel 已初始化的逻辑通道。
 * @return OK 或参数、状态、占用、计时错误。 */
DevFlameResult BSP_Flame_Clear(DevFlameChannel channel);

#endif
