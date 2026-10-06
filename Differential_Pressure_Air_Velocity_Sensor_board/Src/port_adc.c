#include "port_adc.h"
#include "bsp_adc.h"
#include <stdbool.h>
#include <stddef.h>

static DevAdc s_adc; // 保存本工程唯一的ADC接口对象。
static bool s_adc_initialized = false; // 保存设备是否完成初始化及校准。

/**
 * @brief 实现ADC初始化接口，将板级初始化结果转换为设备状态。
 * @param self GetAdc返回的唯一设备对象。
 * @return 成功DEV_OK，参数错误DEV_ARGUMENT_ERROR，超时DEV_TIMEOUT，其他板级失败DEV_IO_ERROR。
 */
static DevStatus port_adc_init(DevAdc *self)
{
    BspAdcStatus result; // 保存板级初始化和校准返回值。
    if (self != &s_adc) return DEV_ARGUMENT_ERROR;
    s_adc_initialized = false;
    result = BSP_Adc_Init();
    if (result == BSP_ADC_TIMEOUT) return DEV_TIMEOUT;
    if (result != BSP_ADC_OK) return DEV_IO_ERROR;
    s_adc_initialized = true;
    return DEV_OK;
}

/**
 * @brief 实现成对原始数据读取接口，失败时保留调用者的原输出。
 * @param self GetAdc返回的唯一设备对象。
 * @param output 接收两路原始采样值的非空指针。
 * @return 成功DEV_OK，参数错误DEV_ARGUMENT_ERROR，未就绪DEV_NOT_READY，超时DEV_TIMEOUT，其他板级失败DEV_IO_ERROR。
 */
static DevStatus port_adc_read_pair(DevAdc *self, DevAdcRawPair *output)
{
    DevAdcRawPair pair; // 保存待完整提交的两路原始码。
    BspAdcStatus result; // 保存板级成对读取结果。
    if (self != &s_adc || output == NULL) return DEV_ARGUMENT_ERROR;
    if (!s_adc_initialized) return DEV_NOT_READY;
    result = BSP_Adc_ReadPair(&pair.signal_raw, &pair.reference_raw);
    if (result != BSP_ADC_OK) {
        s_adc_initialized = false;
        if (result == BSP_ADC_TIMEOUT) return DEV_TIMEOUT;
        if (result == BSP_ADC_NOT_READY) return DEV_NOT_READY;
        return DEV_IO_ERROR;
    }
    *output = pair;
    return DEV_OK;
}

/**
 * @brief 绑定已批准的ADC初始化和成对读取方法，不执行硬件操作。
 * @return 本工程唯一的非空ADC接口对象。
 */
DevAdc *GetAdc(void)
{
    s_adc.init = port_adc_init;
    s_adc.readPair = port_adc_read_pair;
    return &s_adc;
}
