#ifndef DEV_METHANE_H
#define DEV_METHANE_H

#include <stdint.h>
#include "dev_common.h"

/**
 * @brief 甲烷传感器一次有效响应中的测量值和状态码。
 */
typedef struct {
    uint16_t concentration_centi_vol; /**< 甲烷体积分数，单位为 0.01 vol%。 */
    int16_t temperature_deci_c;       /**< 温度，单位为 0.1 摄氏度。 */
    uint32_t pressure_centi_mbar;     /**< 压力，单位为 0.01 mbar。 */
    uint8_t status_code;              /**< 传感器状态码；0 表示传感器报告正常。 */
} MethaneSample;

/**
 * @brief 传感器支持的安全命令集合；枚举值对应发送到传感器的命令文本。
 */
typedef enum {
    METHANE_CMD_R0, /**< 命令文本 R0。 */
    METHANE_CMD_R2, /**< 命令文本 R2。 */
    METHANE_CMD_R4, /**< 命令文本 R4。 */
    METHANE_CMD_R6, /**< 命令文本 R6。 */
    METHANE_CMD_R8, /**< 命令文本 R8，用于请求测量样本。 */
    METHANE_CMD_RA, /**< 命令文本 RA。 */
    METHANE_CMD_RC, /**< 命令文本 RC。 */
    METHANE_CMD_F0, /**< 命令文本 F0。 */
    METHANE_CMD_F1, /**< 命令文本 F1。 */
    METHANE_CMD_F4, /**< 命令文本 F4。 */
    METHANE_CMD_S1, /**< 命令文本 S1。 */
    METHANE_CMD_S2  /**< 命令文本 S2。 */
} MethaneSafeCommand;

/**
 * @brief 提供甲烷传感器串口命令和样本读取能力的设备接口。
 */
typedef struct DevMethane {
    /**
     * @brief 初始化甲烷传感器串口和响应状态。
     * @param self 接收初始化请求的甲烷传感器设备对象。
     */
    void (*init)(struct DevMethane *self);

    /**
     * @brief 发送一个受支持的传感器命令。
     * @param self 接收命令请求的甲烷传感器设备对象。
     * @param command 要发送的命令，必须取自 MethaneSafeCommand。
     * @return 发送成功返回 DEV_OK；命令无效返回 DEV_FORMAT_ERROR；底层串口写入失败返回 DEV_IO_ERROR。
     */
    DevStatus (*sendSafeCommand)(struct DevMethane *self, MethaneSafeCommand command);

    /**
     * @brief 清除旧响应并请求一组新的甲烷测量样本。
     * @param self 接收请求的甲烷传感器设备对象。
     * @return 请求发送成功返回 DEV_OK；命令构造或串口写入失败时返回相应错误状态。
     */
    DevStatus (*requestSample)(struct DevMethane *self);

    /**
     * @brief 读取并解析当前可用的甲烷传感器响应数据。
     * @param self 接收轮询请求的甲烷传感器设备对象。
     * @param sample 接收完整有效测量样本的输出对象，不得为空指针。
     * @return 响应尚未完整到达返回 DEV_PENDING；解析成功返回 DEV_OK；格式、LRC 校验或传感器状态异常时返回对应错误码。
     */
    DevStatus (*pollSample)(struct DevMethane *self, MethaneSample *sample);

    /**
     * @brief 丢弃当前未完成响应以及 UART 中待处理的旧数据。
     * @param self 接收清理请求的甲烷传感器设备对象。
     */
    void (*resetResponse)(struct DevMethane *self);
} DevMethane;

/**
 * @brief 获取甲烷传感器设备接口。
 * @return 指向静态甲烷传感器设备接口的指针。
 */
DevMethane *GetMethane(void);

#endif
