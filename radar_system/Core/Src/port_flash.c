#include "port_flash.h"
#include "bsp_flash.h"
#include <stddef.h>
static DevFlash s_flash; // 唯一设备方法表。
/** @brief 初始化Flash。 @param self 唯一设备对象。 @return 操作结果。 */
static DevFlashStatus flash_init(DevFlash *self)
{
 return self == &s_flash ? BSP_Flash_Init() : DEV_FLASH_ARGUMENT;
}
/** @brief 读取保留区相对地址。 @param self 唯一设备对象。 @param offset 字节偏移。 @param data 输出数组。 @param length 字节数。 @return 操作结果。 */
static DevFlashStatus flash_read(DevFlash *self, uint32_t offset, uint8_t *data, uint32_t length)
{
 if (self != &s_flash || data == NULL || length == 0U ||
     offset >= DEV_FLASH_CAPACITY || length > DEV_FLASH_CAPACITY - offset)
     return DEV_FLASH_ARGUMENT;
 return BSP_Flash_Read(offset, data, length);
}
/** @brief 擦除一页。 @param self 唯一设备对象。 @param pageIndex 保留区页号。 @return 操作结果。 */
static DevFlashStatus flash_erase_page(DevFlash *self, uint32_t pageIndex)
{
 if (self != &s_flash || pageIndex >= DEV_FLASH_PAGE_COUNT) return DEV_FLASH_ARGUMENT;
 return BSP_Flash_ErasePage(pageIndex);
}
/** @brief 编程半字数组。 @param self 唯一设备对象。 @param offset 偶数字节偏移。 @param data 输入半字数组。 @param halfWordCount 半字数。 @return 操作结果。 */
static DevFlashStatus flash_program_half_words(DevFlash *self, uint32_t offset, const uint16_t *data, uint32_t halfWordCount)
{
 if (self != &s_flash || data == NULL || halfWordCount == 0U ||
     (offset % sizeof(uint16_t)) != 0U || offset >= DEV_FLASH_CAPACITY ||
     halfWordCount > (DEV_FLASH_CAPACITY - offset) / sizeof(uint16_t))
     return DEV_FLASH_ARGUMENT;
 return BSP_Flash_ProgramHalfWords(offset, data, halfWordCount);
}
/** @brief 绑定设备方法，不操作硬件。 @return 单实例对象。 */
DevFlash *GetFlash(void)
{
 s_flash.init = flash_init;
 s_flash.read = flash_read;
 s_flash.erasePage = flash_erase_page;
 s_flash.programHalfWords = flash_program_half_words;
 return &s_flash;
}
