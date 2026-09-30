#ifndef DEV_COMMON_H
#define DEV_COMMON_H

/**
 * @brief 设备接口统一使用的操作状态码。
 */
typedef enum {
    DEV_OK = 0,         /**< 操作成功完成。 */
    DEV_PENDING,        /**< 操作尚未完成，调用方可稍后继续查询。 */
    DEV_TIMEOUT,        /**< 操作等待响应超过规定时限。 */
    DEV_FORMAT_ERROR,   /**< 输入数据或响应帧格式无效。 */
    DEV_LRC_ERROR,      /**< 响应帧的 LRC 校验失败。 */
    DEV_SENSOR_ERROR,   /**< 响应有效，但传感器报告故障状态。 */
    DEV_IO_ERROR        /**< 底层外设读写或初始化失败。 */
} DevStatus;

#endif
