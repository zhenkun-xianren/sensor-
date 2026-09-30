#ifndef BSP_UART3_H
#define BSP_UART3_H

#include <stdint.h>

#define BSP_UART3_APB1_CLOCK_HZ 36000000UL
#define BSP_UART3_BAUD_RATE 9600U
#define BSP_UART3_BRR_VALUE (BSP_UART3_APB1_CLOCK_HZ / BSP_UART3_BAUD_RATE)
#define BSP_UART3_TX_READY_POLL_LIMIT 1000000UL
#define BSP_UART3_IRQ_PRIORITY 6U

void BSP_Uart3_Init(void);
uint8_t BSP_Uart3_Write(const uint8_t *data, uint8_t length);
uint8_t BSP_Uart3_Read(uint8_t *data);
uint8_t BSP_Uart3_TakeOverrun(void);
void BSP_Uart3_IrqHandler(void);

#endif
