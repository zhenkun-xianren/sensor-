#ifndef BSP_FLASH_H
#define BSP_FLASH_H
#include "dev_flash.h"
/** @brief 核对容量、HSI和DWT条件。 @return 板级初始化结果。 */
DevFlashStatus BSP_Flash_Init(void);
/** @brief 读取保留区。 @param offset 相对字节偏移。 @param data 输出缓冲区。 @param length 字节数。 @return 操作结果。 */
DevFlashStatus BSP_Flash_Read(uint32_t offset, uint8_t *data, uint32_t length);
/** @brief 擦除保留区的一页。 @param pageIndex 相对页号。 @return 操作结果。 */
DevFlashStatus BSP_Flash_ErasePage(uint32_t pageIndex);
/** @brief 在保留区编程半字。 @param offset 相对偶数字节偏移。 @param data 半字数组。 @param halfWordCount 半字数量。 @return 操作结果。 */
DevFlashStatus BSP_Flash_ProgramHalfWords(uint32_t offset, const uint16_t *data, uint32_t halfWordCount);
#endif
