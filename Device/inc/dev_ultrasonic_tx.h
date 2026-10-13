#ifndef DEV_ULTRASONIC_TX_H
#define DEV_ULTRASONIC_TX_H

#include <stdint.h>
#include "dev_common.h"

/** @brief 第1路DRV8874固定40kHz发射设备，不包含板级引脚或电压反馈。 */
typedef struct DevUltrasonicTx DevUltrasonicTx;

/** @brief 设备方法由Port静态绑定；取得对象后必须显式初始化。 */
struct DevUltrasonicTx {
    /**
     * @brief 初始化发射设备；首次成功后保持停止，重复调用不重置已初始化设备。
     * @param self GetUltrasonicTx返回的设备对象。
     * @return 成功DEV_OK，对象错误DEV_ARGUMENT_ERROR，板级配置失败DEV_IO_ERROR。
     */
    DevStatus (*init)(DevUltrasonicTx *self);
    /**
     * @brief 启动第1路40kHz、50%占空比连续输出；已运行时不重置波形。
     * @param self 已显式初始化成功的设备对象。
     * @return 成功DEV_OK，正在启动DEV_PENDING，对象错误DEV_ARGUMENT_ERROR，
     *         未初始化或启动被停止取消DEV_NOT_READY，唤醒计时超时DEV_TIMEOUT，
     *         非任务上下文、时钟或外设配置异常DEV_IO_ERROR。
     */
    DevStatus (*startContinuous)(DevUltrasonicTx *self);
    /**
     * @brief 关闭门控、定时器及唤醒信号；重复调用保持停止，可取消正在唤醒的启动。
     * @param self 已显式初始化成功的设备对象。
     * @return 关闭请求完成DEV_OK，对象错误DEV_ARGUMENT_ERROR，未初始化DEV_NOT_READY，
     *         板级关闭核对失败DEV_IO_ERROR；成功不代表实物反馈已经验证。
     */
    DevStatus (*stop)(DevUltrasonicTx *self);
    /**
     * @brief 发送一次有限周期40kHz脉冲串；重复请求运行中的同一脉冲串不重置它。
     * @param self 已显式初始化的设备对象。
     * @param cycleCount 脉冲串周期数，范围1至256。
     * @return 成功DEV_OK，其他波形正运行或正在唤醒DEV_PENDING，参数错误DEV_ARGUMENT_ERROR，
     *         未就绪DEV_NOT_READY，唤醒超时DEV_TIMEOUT，板级配置异常DEV_IO_ERROR。
     */
    DevStatus (*startBurst)(DevUltrasonicTx *self, uint16_t cycleCount);
};

/**
 * @brief 返回唯一静态发射接口，不初始化硬件、不重置状态、不分配内存。
 * @return 第1路发射设备接口，重复获取返回同一对象。
 */
DevUltrasonicTx *GetUltrasonicTx(void);

#endif
