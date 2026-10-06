#ifndef DEV_ADC_H
#define DEV_ADC_H

#include <stdint.h>
#include "dev_common.h"

#define DEV_ADC_RAW_MAX 4095U /**< 本接口12位原始码的最大值，饱和码由业务判定。 */

/** @brief 同一次读取取得的两路ADC原始值；不包含电压换算。 */
typedef struct {
    uint16_t signal_raw; /**< 测量信号通道原始码，范围0~4095。 */ // 保存测量通道的原始采样值。
    uint16_t reference_raw; /**< 外部参考通道原始码，范围0~4095。 */ // 保存参考通道的原始采样值。
} DevAdcRawPair;

/** @brief ADC设备契约；仅允许一个任务串行初始化和读取，不供中断调用。 */
typedef struct DevAdc DevAdc;
/** @brief 成对采样能力，不暴露芯片引脚、寄存器或操作系统对象。 */
struct DevAdc {
    /**
     * @brief 初始化或重新初始化ADC，并完成上电校准。
     * @param self GetAdc返回的唯一设备对象。
     * @return 成功DEV_OK，对象错误DEV_ARGUMENT_ERROR，等待超时DEV_TIMEOUT，板级失败DEV_IO_ERROR。
     */
    DevStatus (*init)(DevAdc *self); // 初始化设备的方法。
    /**
     * @brief 依次采集测量信号和参考信号，全部成功后提交完整数据对。
     * @param self GetAdc返回的唯一设备对象。
     * @param output 非空输出指针；失败时内容不变，成功时两个字段均为本次采样结果。
     * @return 成功DEV_OK，参数错误DEV_ARGUMENT_ERROR，未就绪DEV_NOT_READY，超时DEV_TIMEOUT，板级失败DEV_IO_ERROR；超时或板级失败后须重新初始化。
     */
    DevStatus (*readPair)(DevAdc *self, DevAdcRawPair *output); // 成对读取原始数据的方法。
};

/**
 * @brief 获取并绑定静态ADC对象，不配置硬件，也不创建任务。
 * @return 本工程唯一的非空ADC对象，设备就绪状态由init的结果确定。
 */
DevAdc *GetAdc(void);
#endif
