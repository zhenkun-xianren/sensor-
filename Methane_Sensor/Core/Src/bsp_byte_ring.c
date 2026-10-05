/* 从本地驱动库 共用/Core 复制的环形缓冲区，补齐中文变量注释。 */
#include "bsp_byte_ring.h"
/**
 * @brief 清空环形缓冲区。
 * @param ring 要清空的缓冲区对象。
 */
void BSP_ByteRing_Init(BspByteRing *ring)
{
    ring->head = 0U;
    ring->tail = 0U;
}
/**
 * @brief 从中断生产者写入字节。
 * @param ring 缓冲区对象。
 * @param byte 要写入的字节。
 * @return 成功返回 1，已满返回 0。
 */
uint8_t BSP_ByteRing_Push(BspByteRing *ring, uint8_t byte)
{
    uint8_t next = (uint8_t)((ring->head + 1U) & (BSP_BYTE_RING_SIZE - 1U)); // 写入后的索引。
    if (next == ring->tail) {
        return 0U;
    }
    ring->data[ring->head] = byte;
    ring->head = next;
    return 1U;
}
/**
 * @brief 从任务消费者读取字节。
 * @param ring 缓冲区对象。
 * @param byte 接收读取结果的地址。
 * @return 成功返回 1，空缓冲区返回 0。
 */
uint8_t BSP_ByteRing_Pop(BspByteRing *ring, uint8_t *byte)
{
    uint8_t tail; // 本次读取时的消费者索引。
    if (ring->tail == ring->head) {
        return 0U;
    }
    tail = ring->tail;
    *byte = ring->data[tail];
    ring->tail = (uint8_t)((tail + 1U) & (BSP_BYTE_RING_SIZE - 1U));
    return 1U;
}

