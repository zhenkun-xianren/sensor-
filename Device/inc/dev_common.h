#ifndef DEV_COMMON_H
#define DEV_COMMON_H

/**
 * @brief 设备接口统一使用的操作状态码。
 */
typedef enum {
    DEV_OK = 0,         /**< 操作成功完成。 */
    DEV_PENDING = 1,        /**< 操作尚未完成，调用方可稍后继续查询。 */
    DEV_TIMEOUT = 2,        /**< 操作等待响应超过规定时限。 */
    DEV_FORMAT_ERROR = 3,   /**< 输入数据或响应帧格式无效。 */
    DEV_LRC_ERROR = 4,      /**< 响应帧的 LRC 校验失败。 */
    DEV_SENSOR_ERROR = 5,   /**< 响应有效，但传感器报告故障状态。 */
    DEV_IO_ERROR = 6,   /**< 底层外设读写或初始化失败；保留库原值6。 */
    DEV_ARGUMENT_ERROR = -1, /**< 设备对象、参数或输出指针无效。 */
    DEV_NOT_READY = -2  /**< 所需初始化尚未成功。 */
} DevStatus;

#endif
