#include "bsp_flash.h"
#include "bsp_board.h"
#include "stm32f1xx.h"
#include <stdbool.h>
#include <stddef.h>
#define BSP_FLASH_RESERVED_BASE 0x0800F800UL // STM32F103C8保证64KB的最后两页。
#define BSP_FLASH_SIZE_REGISTER 0x1FFFF7E0UL // 官方容量签名地址，单位KB。
#define BSP_FLASH_REQUIRED_KB 64U
#define BSP_FLASH_ERASED_HALF_WORD 0xFFFFU
#define BSP_FLASH_PROGRAM_TIMEOUT_CYCLES (BSP_BOARD_HCLK_HZ / 1000UL)
#define BSP_FLASH_ERASE_TIMEOUT_CYCLES (BSP_BOARD_HCLK_HZ / 10UL)
#define BSP_FLASH_ERROR_FLAGS (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)
#define BSP_FLASH_CLEAR_FLAGS (BSP_FLASH_ERROR_FLAGS | FLASH_SR_EOP)
#define BSP_FLASH_FORBIDDEN_MODES (FLASH_CR_MER | FLASH_CR_OPTER | FLASH_CR_OPTPG | FLASH_CR_STRT)
#define BSP_FLASH_RESET_KEY (0x5FAUL << SCB_AIRCR_VECTKEY_Pos)
/** @brief RAM执行的Flash操作种类。 */
typedef enum {
 BSP_FLASH_PROGRAM, /**< 编程单个半字。 */
 BSP_FLASH_ERASE /**< 擦除指定页。 */
} BspFlashOperation;
extern uint8_t Load$$LR$$LR_IROM1$$Limit; // 链接器给出的实际装载区末尾，包含RAM代码装载副本。
static bool s_ready; // 初始化条件是否成立。
/**
 * @brief 完全在RAM中发起操作并以DWT计时；忙超时无法安全取Flash指令时在RAM请求复位。
 * @param operation 页擦除或半字编程。
 * @param address 已检查的保留区绝对地址。
 * @param value 待写入半字，擦除时不用。
 * @param timeoutCycles 操作周期数上限。
 * @return 成功、忙、超时或控制器错误；持续忙超时复位而不返回Flash。
 */
__attribute__((section("FLASH_RAMCODE"), noinline))
static DevFlashStatus flash_run_operation(BspFlashOperation operation, uint32_t address,
                                          uint16_t value, uint32_t timeoutCycles)
{
 uint32_t interruptMask = __get_PRIMASK(); // 保存调用前中断屏蔽状态。
 uint32_t startCycles; // 本次硬件操作起始周期。
 uint32_t elapsed; // 保存实际操作周期数。
 DevFlashStatus result = DEV_FLASH_OK; // 本次控制器状态。
 __disable_irq();
 if ((FLASH->SR & FLASH_SR_BSY) != 0U) {
     __set_PRIMASK(interruptMask);
     return DEV_FLASH_BUSY;
 }
 if ((FLASH->CR & BSP_FLASH_FORBIDDEN_MODES) != 0U) {
     FLASH->CR |= FLASH_CR_LOCK;
     __set_PRIMASK(interruptMask);
     return DEV_FLASH_IO;
 }
 if ((FLASH->CR & FLASH_CR_LOCK) != 0U) {
     FLASH->KEYR = FLASH_KEY1;
     FLASH->KEYR = FLASH_KEY2;
 }
 if ((FLASH->CR & FLASH_CR_LOCK) != 0U) {
     __set_PRIMASK(interruptMask);
     return DEV_FLASH_IO;
 }
 FLASH->SR = BSP_FLASH_CLEAR_FLAGS;
 FLASH->CR &= ~(FLASH_CR_PG | FLASH_CR_PER);
 startCycles = DWT->CYCCNT;
 if (operation == BSP_FLASH_ERASE) {
     FLASH->CR |= FLASH_CR_PER;
     FLASH->AR = address;
     FLASH->CR |= FLASH_CR_STRT;
 } else {
     FLASH->CR |= FLASH_CR_PG;
     *(volatile uint16_t *)address = value;
 }
 __DSB();
 while ((FLASH->SR & FLASH_SR_BSY) != 0U) {
     if ((uint32_t)(DWT->CYCCNT - startCycles) >= timeoutCycles) {
         /* 忙状态下不能跳回Flash，也不能调用位于Flash中的复位函数。 */
         SCB->AIRCR = BSP_FLASH_RESET_KEY | (SCB->AIRCR & SCB_AIRCR_PRIGROUP_Msk) |
                      SCB_AIRCR_SYSRESETREQ_Msk;
         __DSB();
         for (;;) { __NOP(); }
     }
 }
 elapsed = (uint32_t)(DWT->CYCCNT - startCycles);
 if ((FLASH->SR & BSP_FLASH_ERROR_FLAGS) != 0U) result = DEV_FLASH_IO;
 else if (elapsed >= timeoutCycles) result = DEV_FLASH_TIMEOUT;
 FLASH->CR &= ~(FLASH_CR_PG | FLASH_CR_PER);
 FLASH->SR = BSP_FLASH_CLEAR_FLAGS;
 FLASH->CR |= FLASH_CR_LOCK;
 __DSB();
 __ISB();
 __set_PRIMASK(interruptMask);
 return result;
}
/** @brief 初始化并核对芯片容量、时钟和计时器。 @return 初始化结果。 */
DevFlashStatus BSP_Flash_Init(void)
{
 uint32_t before; // 用于核对周期计数实际运行。
 s_ready = false;
 if ((uint32_t)&Load$$LR$$LR_IROM1$$Limit > BSP_FLASH_RESERVED_BASE ||
     *(volatile const uint16_t *)BSP_FLASH_SIZE_REGISTER < BSP_FLASH_REQUIRED_KB ||
     SystemCoreClock != BSP_BOARD_HCLK_HZ || (RCC->CR & RCC_CR_HSIRDY) == 0U)
     return DEV_FLASH_BOARD;
 if ((FLASH->SR & FLASH_SR_BSY) != 0U) return DEV_FLASH_BUSY;
 CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
 DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
 before = DWT->CYCCNT;
 __NOP(); __NOP(); __NOP();
 if (DWT->CYCCNT == before) return DEV_FLASH_BOARD;
 FLASH->CR |= FLASH_CR_LOCK;
 s_ready = true;
 return DEV_FLASH_OK;
}
/** @brief 读取限定的保留区，避免加法溢出。 @param offset 字节偏移。 @param data 输出数组。 @param length 字节数。 @return 操作结果。 */
DevFlashStatus BSP_Flash_Read(uint32_t offset, uint8_t *data, uint32_t length)
{
 uint32_t index; // 遍历待读取字节。
 if (data == NULL || length == 0U || offset >= DEV_FLASH_CAPACITY ||
     length > DEV_FLASH_CAPACITY - offset) return DEV_FLASH_ARGUMENT;
 if (!s_ready) return DEV_FLASH_NOT_READY;
 if ((FLASH->SR & FLASH_SR_BSY) != 0U) return DEV_FLASH_BUSY;
 for (index = 0U; index < length; ++index)
     data[index] = *(volatile const uint8_t *)(BSP_FLASH_RESERVED_BASE + offset + index);
 return DEV_FLASH_OK;
}
/** @brief 仅擦除末尾两个1KB页之一并读回核对。 @param pageIndex 保留区相对页号。 @return 操作结果。 */
DevFlashStatus BSP_Flash_ErasePage(uint32_t pageIndex)
{
 uint32_t address; // 已限定的页首地址。
 uint32_t index; // 核对页内所有半字均已擦除。
 DevFlashStatus result; // 保存RAM操作结果。
 if (pageIndex >= DEV_FLASH_PAGE_COUNT) return DEV_FLASH_ARGUMENT;
 if (!s_ready) return DEV_FLASH_NOT_READY;
 address = BSP_FLASH_RESERVED_BASE + pageIndex * DEV_FLASH_PAGE_SIZE;
 result = flash_run_operation(BSP_FLASH_ERASE, address, BSP_FLASH_ERASED_HALF_WORD,
                              BSP_FLASH_ERASE_TIMEOUT_CYCLES);
 if (result != DEV_FLASH_OK) return result;
 for (index = 0U; index < DEV_FLASH_PAGE_SIZE; index += sizeof(uint16_t))
     if (*(volatile const uint16_t *)(address + index) != BSP_FLASH_ERASED_HALF_WORD)
         return DEV_FLASH_IO;
 return DEV_FLASH_OK;
}
/** @brief 仅在保留区写入空白半字或跳过相同值，逐半字读回。 @param offset 相对偶数字节偏移。 @param data 半字数组。 @param halfWordCount 半字数量。 @return 操作结果。 */
DevFlashStatus BSP_Flash_ProgramHalfWords(uint32_t offset, const uint16_t *data, uint32_t halfWordCount)
{
 uint32_t index; // 遍历已限定范围内的半字。
 uint32_t address; // 当前编程地址。
 uint16_t previous; // 保存编程前值，拒绝覆盖非空白记录。
 DevFlashStatus result; // 保存单次编程结果。
 if (data == NULL || halfWordCount == 0U || offset >= DEV_FLASH_CAPACITY ||
     (offset % sizeof(uint16_t)) != 0U ||
     halfWordCount > (DEV_FLASH_CAPACITY - offset) / sizeof(uint16_t))
     return DEV_FLASH_ARGUMENT;
 if (!s_ready) return DEV_FLASH_NOT_READY;
 if ((FLASH->SR & FLASH_SR_BSY) != 0U) return DEV_FLASH_BUSY;
 /* 先检查整个目的范围，避免参数/非空白错误导致局部改写。 */
 for (index = 0U; index < halfWordCount; ++index) {
     previous = *(volatile const uint16_t *)(BSP_FLASH_RESERVED_BASE + offset + index * sizeof(uint16_t));
     if (previous != BSP_FLASH_ERASED_HALF_WORD && previous != data[index]) return DEV_FLASH_IO;
 }
 for (index = 0U; index < halfWordCount; ++index) {
     address = BSP_FLASH_RESERVED_BASE + offset + index * sizeof(uint16_t);
     if (*(volatile const uint16_t *)address == data[index]) continue;
     result = flash_run_operation(BSP_FLASH_PROGRAM, address, data[index], BSP_FLASH_PROGRAM_TIMEOUT_CYCLES);
     if (result != DEV_FLASH_OK) return result;
     if (*(volatile const uint16_t *)address != data[index]) return DEV_FLASH_IO;
 }
 return DEV_FLASH_OK;
}
