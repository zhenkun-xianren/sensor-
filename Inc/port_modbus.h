#ifndef PORT_MODBUS_H
#define PORT_MODBUS_H

#include <stdint.h>

#define PORT_MODBUS_MIN_SLAVE_ADDRESS       1U
#define PORT_MODBUS_MAX_SLAVE_ADDRESS       247U
#define PORT_MODBUS_FUNCTION_WRITE_SINGLE   0x06U
#define PORT_MODBUS_REQUEST_LENGTH          8U
#define PORT_MODBUS_CRC_DATA_LENGTH         6U
#define PORT_MODBUS_CRC_INITIAL_VALUE       0xFFFFU
#define PORT_MODBUS_CRC_POLYNOMIAL          0xA001U
#define PORT_MODBUS_CRC_BITS_PER_BYTE       8U

/**
 * @brief 初始化 RS485 板级串口。
 * @return 初始化成功返回 1，底层串口配置失败返回 0。
 */
uint8_t PORT_Modbus_Init(void);

/**
 * @brief 构造 Modbus RTU 功能码 0x06 的单寄存器写请求帧。
 * @param slaveAddress 从站地址，范围为 1 至 247。
 * @param registerAddress 目标保持寄存器地址。
 * @param value 写入的 16 位数值。
 * @param frame 用于保存 8 字节 RTU 请求帧的输出缓冲区。
 * @param capacity 输出缓冲区容量，单位为字节。
 * @param frameLength 用于保存生成帧长度的输出指针。
 * @return 请求帧构造成功返回 1；地址或输出参数无效、容量不足时返回 0。
 */
uint8_t PORT_Modbus_BuildWriteSingleRegisterRequest(uint8_t slaveAddress,
                                                    uint16_t registerAddress,
                                                    uint16_t value,
                                                    uint8_t *frame,
                                                    uint8_t capacity,
                                                    uint8_t *frameLength);

#endif /* PORT_MODBUS_H */
