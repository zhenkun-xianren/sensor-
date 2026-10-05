#ifndef BSP_BYTE_RING_H
#define BSP_BYTE_RING_H
#include <stdint.h>
#define BSP_BYTE_RING_SIZE 128U

/** @brief 单中断生产者、单任务消费者的固定字节环形缓冲区。 */
typedef struct {
    volatile uint8_t data[BSP_BYTE_RING_SIZE]; // 环形缓冲区的字节存储。
    volatile uint8_t head; // 中断生产者下一次写入位置。
    volatile uint8_t tail; // 任务消费者下一次读取位置。
} BspByteRing;
/**
 * @brief 清空环形缓冲区，调用者必须排除并发生产。
 * @param ring 要清空的缓冲区对象。
 */
void BSP_ByteRing_Init(BspByteRing *ring);
/**
 * @brief 从唯一生产者写入一个字节。
 * @param ring 缓冲区对象。
 * @param byte 待写入字节。
 * @return 写入成功返回 1，缓冲区已满返回 0。
 */
uint8_t BSP_ByteRing_Push(BspByteRing *ring, uint8_t byte);
/**
 * @brief 从唯一消费者读取一个字节。
 * @param ring 缓冲区对象。
 * @param byte 读取字节的输出地址。
 * @return 读到数据返回 1，缓冲区为空返回 0。
 */
uint8_t BSP_ByteRing_Pop(BspByteRing *ring, uint8_t *byte);
#endif

