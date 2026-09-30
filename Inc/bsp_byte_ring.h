#ifndef BSP_BYTE_RING_H
#define BSP_BYTE_RING_H

#include <stdint.h>

#define BSP_BYTE_RING_SIZE 128U

#if ((BSP_BYTE_RING_SIZE == 0U) || \
     ((BSP_BYTE_RING_SIZE & (BSP_BYTE_RING_SIZE - 1U)) != 0U))
#error "BSP_BYTE_RING_SIZE must be a non-zero power of two."
#endif

/**
 * @brief 固定容量的单生产者、单消费者字节环形缓冲区。
 */
typedef struct {
    volatile uint8_t data[BSP_BYTE_RING_SIZE]; /**< 保存接收字节；一个槽位保留用于区分满与空。 */
    volatile uint8_t head; /**< 生产者下一次写入的位置，可由中断更新。 */
    volatile uint8_t tail; /**< 消费者下一次读取的位置，可由任务更新。 */
} BspByteRing;

/**
 * @brief 初始化环形缓冲区的读写位置。
 * @param ring 要初始化的缓冲区对象；空指针时不执行操作。
 */
void BSP_ByteRing_Init(BspByteRing *ring);

/**
 * @brief 在环形缓冲区未满时写入一个字节。
 * @param ring 接收字节的缓冲区对象。
 * @param byte 要保存的字节值。
 * @return 写入成功返回 1；缓冲区已满或参数为空返回 0。
 */
uint8_t BSP_ByteRing_Push(BspByteRing *ring, uint8_t byte);

/**
 * @brief 在环形缓冲区非空时取出一个字节。
 * @param ring 提供待读字节的缓冲区对象。
 * @param byte 用于接收取出字节的输出指针。
 * @return 读取成功返回 1；缓冲区为空或参数为空返回 0。
 */
uint8_t BSP_ByteRing_Pop(BspByteRing *ring, uint8_t *byte);

#endif
