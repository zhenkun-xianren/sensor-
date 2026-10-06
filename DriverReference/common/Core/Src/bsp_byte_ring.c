#include "bsp_byte_ring.h"

/**
 * @brief 重置字节环形缓冲区的读写索引。
 * @param ring 要初始化的环形缓冲区。
 */
void BSP_ByteRing_Init(BspByteRing *ring)
{
    ring->head = 0U;
    ring->tail = 0U;
}

/**
 * @brief 环形缓冲区未满时写入一个字节。
 * @param ring 接收字节的环形缓冲区。
 * @param byte 要写入的字节。
 * @return 写入成功返回 1，缓冲区已满返回 0。
 */
uint8_t BSP_ByteRing_Push(BspByteRing *ring, uint8_t byte)
{
    uint8_t next = (uint8_t)((ring->head + 1U) & (BSP_BYTE_RING_SIZE - 1U));
    if (next == ring->tail) return 0U;
    ring->data[ring->head] = byte;
    ring->head = next;
    return 1U;
}

/**
 * @brief 环形缓冲区非空时读取一个字节。
 * @param ring 提供待读字节的环形缓冲区。
 * @param byte 接收读出的字节。
 * @return 读取成功返回 1，缓冲区为空返回 0。
 */
uint8_t BSP_ByteRing_Pop(BspByteRing *ring, uint8_t *byte)
{
    uint8_t tail;
    if (ring->tail == ring->head) return 0U;
    tail = ring->tail;
    *byte = ring->data[tail];
    ring->tail = (uint8_t)((tail + 1U) & (BSP_BYTE_RING_SIZE - 1U));
    return 1U;
}
