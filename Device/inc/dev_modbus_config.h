#ifndef DEV_MODBUS_CONFIG_H
#define DEV_MODBUS_CONFIG_H

/* 所有编译单元统一使用本配置；工程可在目标宏中选择功能，库默认保留全部能力。 */
#ifndef DEV_MODBUS_ENABLE_SEND
#define DEV_MODBUS_ENABLE_SEND 1
#endif
#ifndef DEV_MODBUS_ENABLE_RESPONSE
#define DEV_MODBUS_ENABLE_RESPONSE 1
#endif

#if (DEV_MODBUS_ENABLE_SEND != 0) && (DEV_MODBUS_ENABLE_SEND != 1)
#error DEV_MODBUS_ENABLE_SEND_must_be_0_or_1
#endif
#if (DEV_MODBUS_ENABLE_RESPONSE != 0) && (DEV_MODBUS_ENABLE_RESPONSE != 1)
#error DEV_MODBUS_ENABLE_RESPONSE_must_be_0_or_1
#endif
#if (DEV_MODBUS_ENABLE_SEND == 0) && (DEV_MODBUS_ENABLE_RESPONSE == 0)
#error At_least_one_Modbus_operation_must_be_enabled
#endif

#endif /* DEV_MODBUS_CONFIG_H */
