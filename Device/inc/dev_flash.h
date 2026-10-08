#ifndef DEV_FLASH_H
#define DEV_FLASH_H
#include <stdint.h>
#define DEV_FLASH_PAGE_SIZE 1024UL // 设备可访问的一页字节数。
#define DEV_FLASH_PAGE_COUNT 2UL // 保留区页数。
#define DEV_FLASH_CAPACITY (DEV_FLASH_PAGE_SIZE * DEV_FLASH_PAGE_COUNT)
/** @brief Flash操作结果，接口不暴露芯片寄存器。 */
typedef enum {
 DEV_FLASH_OK = 0, /**< 操作及读回成功。 */
 DEV_FLASH_ARGUMENT = -1, /**< 对象、范围或对齐错误。 */
 DEV_FLASH_NOT_READY = -2, /**< 尚未成功初始化。 */
 DEV_FLASH_BUSY = -3, /**< 控制器正忙，未发起操作。 */
 DEV_FLASH_TIMEOUT = -4, /**< 操作超过期限。 */
 DEV_FLASH_IO = -5, /**< 写保护、编程或读回失败。 */
 DEV_FLASH_BOARD = -6 /**< 容量、时钟或周期计数条件不满足。 */
} DevFlashStatus;
/** @brief 仅访问保留区的Flash设备。 */
typedef struct DevFlash DevFlash;
struct DevFlash {
 /** @brief 初始化设备。 @param self 设备对象。 @return 操作结果。 */
 DevFlashStatus (*init)(DevFlash *self); // 初始化方法。
 /** @brief 读取相对范围。 @param self 设备对象。 @param offset 相对字节偏移。 @param data 输出缓冲区。 @param length 字节数。 @return 操作结果。 */
 DevFlashStatus (*read)(DevFlash *self, uint32_t offset, uint8_t *data, uint32_t length); // 读取方法。
 /** @brief 擦除指定保留页。 @param self 设备对象。 @param pageIndex 相对页号。 @return 操作结果。 */
 DevFlashStatus (*erasePage)(DevFlash *self, uint32_t pageIndex); // 页擦除方法。
 /** @brief 写入半字并读回。 @param self 设备对象。 @param offset 相对偶数字节偏移。 @param data 半字数组。 @param halfWordCount 半字数量。 @return 操作结果。 */
 DevFlashStatus (*programHalfWords)(DevFlash *self, uint32_t offset, const uint16_t *data, uint32_t halfWordCount); // 编程方法。
};
/** @brief 获取设备单实例。 @return 静态设备对象。 */
DevFlash *GetFlash(void);
#endif
