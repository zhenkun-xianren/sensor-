#ifndef DEV_MODBUS_CONFIG_H
#define DEV_MODBUS_CONFIG_H

/* 所有编译单元统一使用本配置；工程通过目标宏选择功能，库默认保留历史写功能，读取按需开启。 */
#ifndef DEV_MODBUS_ENABLE_SEND
#define DEV_MODBUS_ENABLE_SEND 1
#endif
#ifndef DEV_MODBUS_ENABLE_RESPONSE
#define DEV_MODBUS_ENABLE_RESPONSE 1
#endif
#ifndef DEV_MODBUS_ENABLE_READ
#define DEV_MODBUS_ENABLE_READ 0
#endif
#ifndef DEV_MODBUS_ENABLE_WRITE
#define DEV_MODBUS_ENABLE_WRITE DEV_MODBUS_ENABLE_RESPONSE
#endif
#if (DEV_MODBUS_ENABLE_WRITE != 0) && (DEV_MODBUS_ENABLE_WRITE != 1)
#error DEV_MODBUS_ENABLE_WRITE_must_be_0_or_1
#endif
#if DEV_MODBUS_ENABLE_WRITE && !DEV_MODBUS_ENABLE_RESPONSE
#error Modbus_write_requires_response_support
#endif
#if (DEV_MODBUS_ENABLE_READ != 0) && (DEV_MODBUS_ENABLE_READ != 1)
#error DEV_MODBUS_ENABLE_READ_must_be_0_or_1
#endif
#if DEV_MODBUS_ENABLE_READ && !DEV_MODBUS_ENABLE_RESPONSE
#error Modbus_read_requires_response_support
#endif

#if (DEV_MODBUS_ENABLE_SEND != 0) && (DEV_MODBUS_ENABLE_SEND != 1)
#error DEV_MODBUS_ENABLE_SEND_must_be_0_or_1
#endif
#if (DEV_MODBUS_ENABLE_RESPONSE != 0) && (DEV_MODBUS_ENABLE_RESPONSE != 1)
#error DEV_MODBUS_ENABLE_RESPONSE_must_be_0_or_1
#endif
#if !DEV_MODBUS_ENABLE_SEND && !DEV_MODBUS_ENABLE_WRITE && !DEV_MODBUS_ENABLE_READ
#error At_least_one_Modbus_operation_must_be_enabled
#endif

#endif /* DEV_MODBUS_CONFIG_H */
