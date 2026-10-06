#ifndef BSP_BYTE_RING_H
#define BSP_BYTE_RING_H

#include <stdint.h>

#define BSP_BYTE_RING_SIZE 128U

typedef struct {
    volatile uint8_t data[BSP_BYTE_RING_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
} BspByteRing;

void BSP_ByteRing_Init(BspByteRing *ring);
uint8_t BSP_ByteRing_Push(BspByteRing *ring, uint8_t byte);
uint8_t BSP_ByteRing_Pop(BspByteRing *ring, uint8_t *byte);

#endif
