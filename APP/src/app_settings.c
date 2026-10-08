#include "app_settings.h"
#include "dev_flash.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stddef.h>
#define SETTINGS_RECORD_BYTES 16UL
#define SETTINGS_RECORD_HALF_WORDS 8UL
#define SETTINGS_BODY_HALF_WORDS 7UL
#define SETTINGS_COMMIT_HALF_WORDS 1UL
#define SETTINGS_CRC_BYTES 12U
#define SETTINGS_MAGIC 0x52414452UL
#define SETTINGS_FORMAT 1U
#define SETTINGS_COMMIT 0xA55AU
#define SETTINGS_ERASED_BYTE 0xFFU
#define SETTINGS_CRC_INITIAL 0xFFFFU
#define SETTINGS_CRC_POLYNOMIAL 0xA001U
#define SETTINGS_CRC_BITS 8U
#define SETTINGS_CRC_LOW_BIT 1U
#define SETTINGS_SEQUENCE_HALF_RANGE 0x80000000UL
#define SETTINGS_COMMAND_CAPACITY 1U
#define SETTINGS_EVENT_CAPACITY 2U
#define SETTINGS_STACK_WORDS 512U
#define SETTINGS_TASK_PRIORITY 2U
#define SETTINGS_NO_WAIT 0U
#define SETTINGS_LOCK_WAIT_TICKS pdMS_TO_TICKS(250U)
#define SETTINGS_DISPLAY_WAIT_TICKS pdMS_TO_TICKS(500U)
#define SETTINGS_PAUSE_ALLOWED 1UL
#define SETTINGS_PAUSE_DENIED 2UL
/** @brief 固定十六字节记录，提交标记最后写入。 */
typedef struct {
 uint32_t magic; // 格式识别。
 uint32_t sequence; // 保存序号，允许自然回绕。
 uint16_t threshold_mm; // 保存值，单位毫米。
 uint16_t version; // 格式版本。
 uint16_t crc; // 前十二字节校验。
 uint16_t commit; // 最后写入的完成标记。
} SettingsRecord;
/** @brief 同一记录的字段、字节和半字视图。 */
typedef union {
 SettingsRecord record; // 字段视图。
 uint8_t bytes[SETTINGS_RECORD_BYTES]; // CRC及读取视图。
 uint16_t half_words[SETTINGS_RECORD_HALF_WORDS]; // 编程视图。
} SettingsRecordBuffer;
typedef char SettingsRecordSizeCheck[(sizeof(SettingsRecord) == SETTINGS_RECORD_BYTES) ? 1 : -1]; // 编译时核对布局。
typedef char SettingsCommitOffsetCheck[(offsetof(SettingsRecord, commit) == SETTINGS_BODY_HALF_WORDS * sizeof(uint16_t)) ? 1 : -1]; // 核对提交标记地址。
typedef char SettingsCrcOffsetCheck[(offsetof(SettingsRecord, crc) == SETTINGS_CRC_BYTES) ? 1 : -1]; // 核对校验覆盖范围。
static DevFlash *s_flash; // 保存任务使用的设备接口。
static StaticSemaphore_t s_mutex_storage; // 完整事务与Flash共享互斥锁。
static SemaphoreHandle_t s_mutex; // 互斥锁句柄。
static StaticQueue_t s_command_storage; // 保存命令静态控制块。
static uint8_t s_command_bytes[SETTINGS_COMMAND_CAPACITY * sizeof(uint16_t)]; // 保存命令内容。
static QueueHandle_t s_commands; // 保存命令队列。
static StaticQueue_t s_event_storage; // 阶段消息静态控制块。
static uint8_t s_event_bytes[SETTINGS_EVENT_CAPACITY * sizeof(AppSettingsEvent)]; // 阶段消息内容。
static QueueHandle_t s_events; // 显示任务读取的阶段队列。
static StaticTask_t s_task_storage; // 保存任务控制块。
static StackType_t s_stack[SETTINGS_STACK_WORDS]; // 保存任务静态栈。
static TaskHandle_t s_task; // 保存任务句柄。
static bool s_started; // 资源是否已成功创建。
static bool s_pending; // 是否已有一次保存尚未交接结果。
static uint16_t s_initial = APP_SETTINGS_DEFAULT_MM; // 开机扫描结果。
static uint16_t s_persisted = APP_SETTINGS_DEFAULT_MM; // 最近确认的持久值。
static uint32_t s_sequence; // 最近有效记录序号。
static uint32_t s_page; // 最新记录所在页，无记录时选择第一页。
static uint32_t s_next; // 最新页所有非空槽之后的相对偏移。
static volatile DevFlashStatus s_last_flash_status = DEV_FLASH_NOT_READY; // 保存最近设备结果。
/** @brief 计算记录CRC。 @param bytes 记录前十二字节。 @return CRC16值。 */
static uint16_t settings_crc(const uint8_t *bytes)
{
 uint16_t crc = SETTINGS_CRC_INITIAL; // 逐位CRC累积值。
 uint32_t index; // 当前字节下标。
 uint32_t bit; // 当前字节位下标。
 for (index = 0U; index < SETTINGS_CRC_BYTES; ++index) {
     crc ^= bytes[index];
     for (bit = 0U; bit < SETTINGS_CRC_BITS; ++bit)
         crc = (uint16_t)((crc >> 1U) ^ ((crc & SETTINGS_CRC_LOW_BIT) != 0U ? SETTINGS_CRC_POLYNOMIAL : 0U));
 }
 return crc;
}
/** @brief 校验完整已提交的记录。 @param buffer 记录缓冲区。 @return 校验通过返回true。 */
static bool settings_valid(const SettingsRecordBuffer *buffer)
{
 return buffer->record.magic == SETTINGS_MAGIC && buffer->record.version == SETTINGS_FORMAT &&
     buffer->record.commit == SETTINGS_COMMIT && buffer->record.threshold_mm <= APP_SETTINGS_MAX_MM &&
     buffer->record.threshold_mm % APP_SETTINGS_STEP_MM == 0U &&
     buffer->record.crc == settings_crc(buffer->bytes);
}
/** @brief 扫描所有槽并选择最新有效记录，不复用部分写入的槽。 @return 所有读取完成返回true。 */
static bool settings_scan(void)
{
 SettingsRecordBuffer buffer; // 当前读取的记录。
 uint32_t offset; // 遍历整个保留区的槽。
 uint32_t index; // 判断记录是否全为空白。
 uint32_t used_end[DEV_FLASH_PAGE_COUNT] = {0U}; // 每页最后非空槽后的偏移。
 uint32_t latest_page = 0U; // 最新有效页。
 uint32_t sequence = 0U; // 最新有效序号。
 uint16_t value = APP_SETTINGS_DEFAULT_MM; // 没有有效记录时的默认值。
 bool found = false; // 是否找到有效记录。
 bool blank; // 当前槽是否全为擦除值。
 for (offset = 0U; offset < DEV_FLASH_CAPACITY; offset += SETTINGS_RECORD_BYTES) {
     s_last_flash_status = s_flash->read(s_flash, offset, buffer.bytes, sizeof(buffer));
     if (s_last_flash_status != DEV_FLASH_OK) return false;
     blank = true;
     for (index = 0U; index < sizeof(buffer); ++index)
         if (buffer.bytes[index] != SETTINGS_ERASED_BYTE) blank = false;
     if (!blank) used_end[offset / DEV_FLASH_PAGE_SIZE] =
         offset % DEV_FLASH_PAGE_SIZE + SETTINGS_RECORD_BYTES;
     if (settings_valid(&buffer) && (!found ||
         ((uint32_t)(buffer.record.sequence - sequence) != 0U &&
          (uint32_t)(buffer.record.sequence - sequence) < SETTINGS_SEQUENCE_HALF_RANGE))) {
         sequence = buffer.record.sequence;
         value = buffer.record.threshold_mm;
         latest_page = offset / DEV_FLASH_PAGE_SIZE;
         found = true;
     }
 }
 s_sequence = sequence;
 s_persisted = value;
 s_page = latest_page;
 s_next = used_end[latest_page];
 return true;
}
/** @brief 追加记录，切页时只擦除旧记录所在页以外的页。 @param value 新设定毫米值。 @return 完整提交并读回成功返回true。 */
static bool settings_save(uint16_t value)
{
 SettingsRecordBuffer buffer; // 待保存的完整记录。
 SettingsRecordBuffer verify; // 提交后的读回记录。
 uint32_t target_page; // 本次写入目标页。
 uint32_t target_offset; // 保留区相对写入起点。
 s_last_flash_status = s_flash->init(s_flash);
 if (s_last_flash_status != DEV_FLASH_OK || !settings_scan()) return false;
 if (value == s_persisted) return true;
 target_page = s_page;
 if (s_next > DEV_FLASH_PAGE_SIZE - SETTINGS_RECORD_BYTES) {
     target_page = (s_page + 1U) % DEV_FLASH_PAGE_COUNT;
     s_last_flash_status = s_flash->erasePage(s_flash, target_page);
     if (s_last_flash_status != DEV_FLASH_OK) return false;
     target_offset = target_page * DEV_FLASH_PAGE_SIZE;
 } else target_offset = target_page * DEV_FLASH_PAGE_SIZE + s_next;
 buffer.record.magic = SETTINGS_MAGIC;
 buffer.record.sequence = s_sequence + 1U;
 buffer.record.threshold_mm = value;
 buffer.record.version = SETTINGS_FORMAT;
 buffer.record.crc = settings_crc(buffer.bytes);
 buffer.record.commit = SETTINGS_COMMIT;
 s_last_flash_status = s_flash->programHalfWords(s_flash, target_offset,
     buffer.half_words, SETTINGS_BODY_HALF_WORDS);
 if (s_last_flash_status != DEV_FLASH_OK) return false;
 /* 提交前读回正文，任何差异均不写完成标记。 */
 s_last_flash_status = s_flash->read(s_flash, target_offset, verify.bytes, sizeof(verify));
 if (s_last_flash_status != DEV_FLASH_OK ||
     verify.record.magic != buffer.record.magic || verify.record.sequence != buffer.record.sequence ||
     verify.record.threshold_mm != value || verify.record.version != SETTINGS_FORMAT ||
     verify.record.crc != buffer.record.crc || verify.record.crc != settings_crc(verify.bytes))
     return false;
 s_last_flash_status = s_flash->programHalfWords(s_flash,
     target_offset + SETTINGS_BODY_HALF_WORDS * sizeof(uint16_t),
     &buffer.half_words[SETTINGS_BODY_HALF_WORDS], SETTINGS_COMMIT_HALF_WORDS);
 if (s_last_flash_status != DEV_FLASH_OK) return false;
 s_last_flash_status = s_flash->read(s_flash, target_offset, verify.bytes, sizeof(verify));
 if (s_last_flash_status != DEV_FLASH_OK || !settings_valid(&verify) ||
     verify.record.sequence != buffer.record.sequence || verify.record.threshold_mm != value)
     return false;
 s_persisted = value;
 s_sequence = buffer.record.sequence;
 s_page = target_page;
 s_next = target_offset % DEV_FLASH_PAGE_SIZE + SETTINGS_RECORD_BYTES;
 return true;
}
/** @brief 串行保存任务，等数码管安全关闭后才实际擦写。 @param argument 保留任务参数。 */
static void settings_task(void *argument)
{
 uint16_t value; // 当前请求的设定值。
 AppSettingsEvent event; // 发布给显示状态机的消息。
 uint32_t approval; // 显示停止的确认值。
 bool success; // 本次保存是否完整成功。
 (void)argument;
 for (;;) {
     if (xQueueReceive(s_commands, &value, portMAX_DELAY) != pdPASS) continue;
     success = false;
     event.threshold_mm = value;
     if (App_Settings_LockBus()) {
         /* 清空先前通知，避免上一轮超时确认被下一轮误用。 */
         (void)ulTaskNotifyTake(pdTRUE, SETTINGS_NO_WAIT);
         event.type = APP_SETTINGS_PREPARE;
         if (xQueueSend(s_events, &event, SETTINGS_NO_WAIT) == pdPASS) {
             approval = ulTaskNotifyTake(pdTRUE, SETTINGS_DISPLAY_WAIT_TICKS);
             if (approval == SETTINGS_PAUSE_ALLOWED) success = settings_save(value);
         }
         App_Settings_UnlockBus();
     }
     event.type = success ? APP_SETTINGS_SAVED : APP_SETTINGS_FAILED;
     /* 仅保存任务生产；容量能同时容纳准备与完成消息。 */
     if (xQueueSend(s_events, &event, portMAX_DELAY) != pdPASS)
         configASSERT(false);
 }
}
/** @brief 建立静态资源并恢复开机设定。 @return 静态资源全部创建成功返回true。 */
bool App_Settings_Start(void)
{
 if (s_started) return true;
 s_flash = GetFlash();
 if (s_flash == NULL || s_flash->init == NULL || s_flash->read == NULL ||
     s_flash->erasePage == NULL || s_flash->programHalfWords == NULL) return false;
 s_last_flash_status = s_flash->init(s_flash);
 if (s_last_flash_status == DEV_FLASH_OK && settings_scan()) s_initial = s_persisted;
 s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
 s_commands = xQueueCreateStatic(SETTINGS_COMMAND_CAPACITY, sizeof(uint16_t), s_command_bytes, &s_command_storage);
 s_events = xQueueCreateStatic(SETTINGS_EVENT_CAPACITY, sizeof(AppSettingsEvent), s_event_bytes, &s_event_storage);
 if (s_mutex == NULL || s_commands == NULL || s_events == NULL) return false;
 s_task = xTaskCreateStatic(settings_task, "flash_save", SETTINGS_STACK_WORDS, NULL,
     SETTINGS_TASK_PRIORITY, s_stack, &s_task_storage);
 s_started = s_task != NULL;
 return s_started;
}
/** @brief 获取上电恢复值。 @return 初始设定毫米值。 */
uint16_t App_Settings_InitialValue(void) { return s_initial; }
/** @brief 提交单次非阻塞保存请求。 @param threshold_mm 保存毫米值。 @return 成功入队返回true。 */
bool App_Settings_RequestSave(uint16_t threshold_mm)
{
 if (!s_started || s_pending || threshold_mm > APP_SETTINGS_MAX_MM ||
     threshold_mm % APP_SETTINGS_STEP_MM != 0U) return false;
 if (xQueueSend(s_commands, &threshold_mm, SETTINGS_NO_WAIT) != pdPASS) return false;
 s_pending = true;
 return true;
}
/** @brief 取阶段消息并在完成交接后解除重复请求限制。 @param event 输出阶段。 @return 存在消息返回true。 */
bool App_Settings_TakeEvent(AppSettingsEvent *event)
{
 if (!s_started || event == NULL || xQueueReceive(s_events, event, SETTINGS_NO_WAIT) != pdPASS) return false;
 if (event->type != APP_SETTINGS_PREPARE) s_pending = false;
 return true;
}
/** @brief 确认显示关闭结果。 @param allowed 是否允许编程。 */
void App_Settings_AllowSave(bool allowed)
{
 (void)xTaskNotify(s_task, allowed ? SETTINGS_PAUSE_ALLOWED : SETTINGS_PAUSE_DENIED, eSetValueWithOverwrite);
}
/** @brief 申请完整事务互斥锁。 @return 取得锁返回true。 */
bool App_Settings_LockBus(void)
{
 return s_mutex != NULL && xSemaphoreTake(s_mutex, SETTINGS_LOCK_WAIT_TICKS) == pdPASS;
}
/** @brief 释放完整事务互斥锁。 */
void App_Settings_UnlockBus(void)
{
 if (s_mutex != NULL) configASSERT(xSemaphoreGive(s_mutex) == pdPASS);
}
