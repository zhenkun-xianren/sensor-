#ifndef DEV_FLAME_H
#define DEV_FLAME_H

#include <stdbool.h>
#include <stdint.h>

/** @brief N8F24 底层操作结果；编译成功不代表硬件通信已验证。 */
typedef enum {
    DEV_FLAME_OK = 0,                  /**< 操作成功，输出有效。 */
    DEV_FLAME_PENDING = 1,             /**< 就绪读取时尚无数据，输出保持不变。 */
    DEV_FLAME_ARGUMENT_ERROR = -1,     /**< 对象、指针、枚举或配置字段无效。 */
    DEV_FLAME_NOT_READY = -2,          /**< 本路尚未成功初始化。 */
    DEV_FLAME_BUSY = -3,               /**< 总线已占用或在中断上下文调用。 */
    DEV_FLAME_TIMEOUT = -4,            /**< 计时基准停止或协议时序超过限制。 */
    DEV_FLAME_CONFIG_MISMATCH = -5,    /**< 25 位配置回读与最近写入不一致。 */
    DEV_FLAME_IO_ERROR = -6            /**< 时钟、GPIO 或定时器配置不符合契约。 */
} DevFlameResult;

/** @brief 板上三路逻辑对象；不表达探头的光谱型号或 GPIO。 */
typedef enum {
    DEV_FLAME_U22 = 0, /**< 位号 U22。 */
    DEV_FLAME_U23 = 1, /**< 位号 U23。 */
    DEV_FLAME_U24 = 2, /**< 位号 U24。 */
    DEV_FLAME_CHANNEL_COUNT = 3 /**< 支持的通道数，不是有效通道。 */
} DevFlameChannel;

/** @brief DOCI 数据读取触发方式。 */
typedef enum {
    DEV_FLAME_READ_READY = 0, /**< DOCI 高时读取，否则返回 PENDING。 */
    DEV_FLAME_READ_FORCE = 1  /**< 按手册强制建立读数，不等待事件。 */
} DevFlameReadMode;

/** @brief 配置 bit7 的中断源；数据就绪不等同于火警。 */
typedef enum {
    DEV_FLAME_INTERRUPT_EVENT = 0, /**< 火焰事件输出。 */
    DEV_FLAME_INTERRUPT_ADC = 1    /**< ADC 数据就绪输出。 */
} DevFlameInterruptSource;

/** @brief 配置 bit6:5 的 ADC/滤波数据源。 */
typedef enum {
    DEV_FLAME_SOURCE_BPF = 0,         /**< 红外带通原始码。 */
    DEV_FLAME_SOURCE_LPF = 1,         /**< 红外低通原始码。 */
    DEV_FLAME_SOURCE_SUPPLY = 2,      /**< 电源电压原始码，换算需要偏置。 */
    DEV_FLAME_SOURCE_TEMPERATURE = 3  /**< 温度原始码，换算需要单点标定。 */
} DevFlameAdcSource;

/** @brief 配置 bit2 的高通截止频率选项。 */
typedef enum {
    DEV_FLAME_HPF_0P4_HZ = 0, /**< 手册名义截止频率 0.4 Hz。 */
    DEV_FLAME_HPF_0P2_HZ = 1  /**< 手册名义截止频率 0.2 Hz。 */
} DevFlameHighPass;

/** @brief 配置 bit0 的脉冲计量方式。 */
typedef enum {
    DEV_FLAME_COUNT_ALTERNATING = 0, /**< 相邻脉冲须改变符号才计数。 */
    DEV_FLAME_COUNT_DIRECT = 1       /**< 每个超过阈值的脉冲直接计数。 */
} DevFlamePulseMode;

/** @brief 应用提供的配置；固定关闭 VREG、自测和输入短接，无隐含阈值。 */
typedef struct {
    uint8_t sensitivity;                 /**< 阈值码 0～255，每步典型 6.5 微伏。 */
    uint8_t blind_time_code;             /**< 0～15，盲锁时间=(码+1)*0.5 秒。 */
    uint8_t pulse_count_code;            /**< 0～3，触发所需脉冲数=码+1。 */
    uint8_t window_time_code;            /**< 0～3，计数窗口=(码+1)*2 秒。 */
    bool detection_enabled;             /**< 是否启用探头内部检测；此时仅允许 BPF/LPF。 */
    DevFlameInterruptSource interrupt_source; /**< DOCI 输出火焰事件或 ADC 数据就绪。 */
    DevFlameAdcSource adc_source;        /**< 本次读取的 ADC/滤波源。 */
    DevFlameHighPass high_pass;          /**< 高通截止频率配置。 */
    DevFlamePulseMode pulse_mode;        /**< 内部检测脉冲计数方式。 */
} DevFlameConfig;

/** @brief 完整 40 位读数；原始码的符号、温度和电压换算留给已标定算法。 */
typedef struct {
    uint16_t adc_raw;     /**< bit38:25 的 14 位原始码，范围 0～16383。 */
    bool over_range;      /**< bit39 为 0 时为真，此时数据不能作为正常测量值。 */
    uint32_t config_raw;  /**< bit24:0 的配置回读，范围 0～0x1FFFFFF。 */
    uint64_t frame_raw;   /**< bit39:0 完整原始帧，高位其余部分为零。 */
} DevFlameSample;

/** @brief 与芯片及 RTOS 无关的设备能力。所有操作由调用方串行调度。 */
typedef struct DevFlame DevFlame;
struct DevFlame {
    /** @brief 初始化本路并写配置、完整回读校验；不得在 ISR 调用。
     * @param self GetFlame 返回的本路对象。
     * @param config 各字段满足上述范围的完整配置，无默认值。
     * @return 成功返回 OK；参数错误不操作硬件，其余失败返回对应错误。 */
    DevFlameResult (*init)(DevFlame *self, const DevFlameConfig *config);
    /** @brief 重新写入配置并校验；失败后本路须重新初始化。
     * @param self 已初始化的本路对象。
     * @param config 新的有效配置。
     * @return OK、ARGUMENT_ERROR、NOT_READY、BUSY、TIMEOUT、CONFIG_MISMATCH 或 IO_ERROR。 */
    DevFlameResult (*configure)(DevFlame *self, const DevFlameConfig *config);
    /** @brief 读取完整帧并核对配置；所有失败及等待状态保持 sample 不变。
     * @param self 已初始化的本路对象。
     * @param mode READY 仅就绪时读取，FORCE 强制读数。
     * @param sample 成功时接收原始码、超范围标志及配置回读。
     * @return 成功返回 OK；未就绪返回 PENDING；其他为参数、状态或板级错误。 */
    DevFlameResult (*read)(DevFlame *self, DevFlameReadMode mode, DevFlameSample *sample);
    /** @brief 查询所配置中断源的 DOCI 电平；不消费事件、不证明发生火警。
     * @param self 已初始化的本路对象。
     * @param pending 成功时接收是否有高电平事件/数据就绪，失败保持不变。
     * @return OK 或对应参数、未初始化、占用、板级错误。 */
    DevFlameResult (*pending)(DevFlame *self, bool *pending);
    /** @brief 按手册拉低并释放 DOCI，主动清除本路事件。
     * @param self 已初始化的本路对象。
     * @return OK 或对应参数、未初始化、占用、计时、板级错误。 */
    DevFlameResult (*clear)(DevFlame *self);
};

/** @brief 获取静态接口对象；不初始化 GPIO，也不写入探头。
 * @param channel U22、U23 或 U24，不接受 COUNT 或其他数值。
 * @return 对应对象；非法通道返回 NULL。 */
DevFlame *GetFlame(DevFlameChannel channel);

#endif
