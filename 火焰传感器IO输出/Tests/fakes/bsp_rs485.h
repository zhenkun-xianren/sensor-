#ifndef BSP_RS485_H
#define BSP_RS485_H
#include <stdint.h>
typedef enum {
    BSP_RS485_OK = 0, BSP_RS485_BUSY = 1, BSP_RS485_NO_FRAME = 2,
    BSP_RS485_INVALID = 3, BSP_RS485_NOT_INITIALIZED = 4, BSP_RS485_TIMEOUT = 5
} BspRs485Status;
BspRs485Status BSP_Rs485_Init(void);
BspRs485Status BSP_Rs485_Poll(void);
BspRs485Status BSP_Rs485_AcquireFrame(const uint8_t **data, uint16_t *length);
void BSP_Rs485_ReleaseFrame(void);
BspRs485Status BSP_Rs485_StartSend(const uint8_t *data, uint16_t length);
#endif
