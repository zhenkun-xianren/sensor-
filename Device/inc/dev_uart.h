#ifndef DEV_UART_H
#define DEV_UART_H

#include <stdint.h>

/** @brief 单实例原始字节串口，不附加文本终止符或协议字段。 */
typedef struct DevUart DevUart;

/** @brief 串口非阻塞操作结果，发送成功仅表示字节进入发送队列。 */
typedef enum {
    DEV_UART_OK = 0,              /**< 初始化成功、读出一个字节或提交一个发送字节。 */
    DEV_UART_EMPTY,               /**< 接收队列暂无字节，输出参数保持不变。 */
    DEV_UART_BUSY,                /**< 发送队列已满，调用方必须保留字节后续重试。 */
    DEV_UART_ERROR_ARGUMENT,      /**< 设备对象、输出指针或调用上下文无效。 */
    DEV_UART_NOT_INITIALIZED,     /**< 设备尚未成功初始化。 */
    DEV_UART_ERROR_RX,            /**< 接收发生硬件错误或队列溢出，此调用不取出字节。 */
    DEV_UART_ERROR_IO             /**< 板级时钟、引脚或串口使能配置不符。 */
} DevUartStatus;

/** @brief 与芯片和操作系统无关的字节串口契约，运行期仅由一个任务调用。 */
struct DevUart {
    /**
     * @brief 初始化串口并清空收发队列，仅在通信开始前调用。
     * @param self GetUart 返回的唯一设备对象。
     * @return 成功返回 DEV_UART_OK，失败返回 ARGUMENT 或 IO 状态。
     */
    DevUartStatus (*init)(DevUart *self); // 初始化设备的方法。
    /**
     * @brief 非阻塞读取一个有效接收字节，接收错误通知不会清空已排队的有效字节。
     * @param self GetUart 返回的唯一设备对象。
     * @param data 接收字节的非空输出指针，仅 OK 时有效，取值为完整的八位数据。
     * @return 返回 OK、EMPTY、ERROR_RX、ARGUMENT、NOT_INITIALIZED 或 IO 状态。
     */
    DevUartStatus (*readByte)(DevUart *self, uint8_t *data); // 非阻塞读取字节的方法。
    /**
     * @brief 非阻塞提交一个发送字节，由后台发送完整的串口字符。
     * @param self GetUart 返回的唯一设备对象。
     * @param data 要原样发送的八位数据，零字节同样有效。
     * @return OK 表示入队；BUSY 表示未入队；其他失败为 ARGUMENT、NOT_INITIALIZED 或 IO。
     */
    DevUartStatus (*writeByte)(DevUart *self, uint8_t data); // 非阻塞提交字节的方法。
};

/**
 * @brief 获取并绑定唯一设备对象，不初始化硬件或清空队列。
 * @return 返回静态串口对象的非空指针。
 */
DevUart *GetUart(void);

#endif /* DEV_UART_H */
