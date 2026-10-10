#ifndef PORT_MODBUS_SLAVE_H
#define PORT_MODBUS_SLAVE_H

#include "dev_modbus_slave.h"

/* 来源主站包的 CRC 参数保留；新增从站帧字段均集中命名。 */
#define PORT_MODBUS_CRC_INITIAL_VALUE       0xFFFFU
#define PORT_MODBUS_CRC_POLYNOMIAL          0xA001U
#define PORT_MODBUS_CRC_BITS_PER_BYTE       8U
#define PORT_MODBUS_CRC_LOW_BIT             0x0001U
#define PORT_MODBUS_CRC_SHIFT               1U
#define PORT_MODBUS_BYTE_SHIFT              8U
#define PORT_MODBUS_CRC_BYTES               2U
#define PORT_MODBUS_MIN_ADU                 4U
#define PORT_MODBUS_READ_REQUEST_LENGTH     8U
#define PORT_MODBUS_ADDRESS_INDEX           0U
#define PORT_MODBUS_FUNCTION_INDEX          1U
#define PORT_MODBUS_START_INDEX             2U
#define PORT_MODBUS_QUANTITY_INDEX          4U
#define PORT_MODBUS_BYTE_COUNT_INDEX        2U
#define PORT_MODBUS_DATA_INDEX              3U
#define PORT_MODBUS_EXCEPTION_INDEX         2U
#define PORT_MODBUS_EXCEPTION_DATA_LENGTH   3U
#define PORT_MODBUS_WORD_BYTES              2U
#define PORT_MODBUS_EXCEPTION_MASK          0x80U
#define PORT_MODBUS_EXCEPTION_FUNCTION      0x01U
#define PORT_MODBUS_EXCEPTION_ADDRESS       0x02U
#define PORT_MODBUS_EXCEPTION_VALUE         0x03U
#define PORT_MODBUS_EXCEPTION_FAILURE       0x04U
#define PORT_MODBUS_MIN_ADDRESS             1U
#define PORT_MODBUS_MAX_ADDRESS             247U
#define PORT_MODBUS_REGISTER_ADDRESS_SPACE  65536UL

#define PORT_MODBUS_RAW_MIN_LENGTH          1U /* 原始主动发送至少一个字节。 */

#endif
