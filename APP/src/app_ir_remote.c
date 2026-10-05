#include "app_ir_remote.h"
#include "dev_ir_remote.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>
#define APP_IR_POLL_PERIOD_MS 10U
#define APP_IR_TASK_STACK_WORDS 256U
#define APP_IR_TASK_PRIORITY 2U
#define APP_IR_HEX_DIGIT_BITS 4U
#define APP_IR_HEX_DIGIT_MASK 0x0FU
typedef struct {
    uint8_t command; // 当前遥控器命令字节。
    const char *name; // 对应屏幕显示的ASCII按键名称。
} AppIrKeyName;
static const AppIrKeyName s_key_names[] = { // 用户确认布局对应的暂定键码，实物尚需逐键核对。
    {0x45U, "CH-"}, {0x46U, "CH"}, {0x47U, "CH+"},
    {0x44U, "PREV"}, {0x40U, "NEXT"}, {0x43U, "PLAY/PAUSE"},
    {0x07U, "-"}, {0x15U, "+"}, {0x09U, "EQ"},
    {0x16U, "0"}, {0x19U, "100+"}, {0x0DU, "200+"},
    {0x0CU, "1"}, {0x18U, "2"}, {0x5EU, "3"},
    {0x08U, "4"}, {0x1CU, "5"}, {0x5AU, "6"},
    {0x42U, "7"}, {0x52U, "8"}, {0x4AU, "9"}
};
static StaticTask_t s_ir_task_tcb; // 独立红外任务的静态控制块。
static StackType_t s_ir_task_stack[APP_IR_TASK_STACK_WORDS]; // 红外任务的静态栈，单位为栈字。
volatile AppIrStatus g_app_ir_status; // 供SWD观察的红外运行状态。
volatile uint32_t g_app_ir_events; // 成功提交显示的有效事件数。
volatile uint32_t g_app_ir_raw_code; // 最近事件的原始帧。
volatile uint16_t g_app_ir_address; // 最近事件的地址。
volatile uint8_t g_app_ir_command; // 最近事件的命令。
volatile uint8_t g_app_ir_repeat; // 最近事件的重复帧标志。
/**
 * @brief 将暂定21键命令表转换为屏幕文字，未知命令仍可观察。
 * @param command NEC 命令字节。
 * @param text 至少22字节的输出缓存。
 * @return 生成成功返回1，空缓存返回0。
 */
uint8_t AppIr_FormatKey(uint8_t command, char text[APP_DISPLAY_TEXT_CAPACITY])
{
    static const char prefix[] = "Key: "; // 第二行固定提示文字。
    static const char hex[] = "0123456789ABCDEF"; // 十六进制数字转换表。
    const char *name = NULL; // 找到的实际按键名称，未找到则显示命令字节。
    uint8_t index; // 遍历命令表和文字的索引。
    uint8_t position = 0U; // 输出缓存的下一写入位置。
    if (text == NULL) return 0U;
    for (index = 0U; index < sizeof(s_key_names) / sizeof(s_key_names[0]); ++index) {
        if (s_key_names[index].command == command) { name = s_key_names[index].name; break; }
    }
    for (index = 0U; prefix[index] != '\0'; ++index) text[position++] = prefix[index];
    if (name != NULL) {
        for (index = 0U; name[index] != '\0'; ++index) text[position++] = name[index];
    } else {
        text[position++] = '0';
        text[position++] = 'x';
        text[position++] = hex[command >> APP_IR_HEX_DIGIT_BITS];
        text[position++] = hex[command & APP_IR_HEX_DIGIT_MASK];
    }
    text[position] = '\0';
    return 1U;
}
/**
 * @brief 标记红外业务失败并停止依赖该设备的任务。
 */
static void ir_fail(void)
{
    g_app_ir_status = APP_IR_ERROR;
    configASSERT(0);
    for (;;) { vTaskDelay(portMAX_DELAY); }
}
/**
 * @brief 每10ms读取有效事件并提交最新按键名称。
 * @param context 保留任务参数，本任务不使用。
 */
static void ir_task(void *context)
{
    DevIrRemote *ir = GetIrRemote(); // 红外设备的硬件无关接口。
    IrKeyEvent event; // 本次读取的完整按键事件。
    DevIrResult result; // 本次读取的成功、等待或失败状态。
    char text[APP_DISPLAY_TEXT_CAPACITY]; // 当前按键的完整第二行文字。
    const TickType_t period = pdMS_TO_TICKS(APP_IR_POLL_PERIOD_MS); // 10ms阻塞读取周期。
    TickType_t wake = xTaskGetTickCount(); // 下次周期唤醒的基准Tick。
    (void)context;
    if (period == 0U || ir == NULL || ir->init == NULL || ir->read == NULL || ir->init(ir) == 0U) {
        ir_fail();
    }
    g_app_ir_status = APP_IR_RUNNING;
    for (;;) {
        do {
            result = ir->read(ir, &event);
            if (result == DEV_IR_ERROR) ir_fail();
            if (result == DEV_IR_EVENT) {
                if (AppIr_FormatKey(event.command, text) == 0U || AppDisplay_SetKeyText(text) == 0U) {
                    ir_fail();
                }
                g_app_ir_raw_code = event.raw_code;
                g_app_ir_address = event.address;
                g_app_ir_command = event.command;
                g_app_ir_repeat = event.repeat;
                ++g_app_ir_events;
            }
        } while (result == DEV_IR_EVENT);
        vTaskDelayUntil(&wake, period);
    }
}
/**
 * @brief 在启动阶段创建红外模块的静态任务。
 * @return 任务创建成功返回1，失败返回0。
 */
uint8_t AppIrRemote_Start(void)
{
    TaskHandle_t task; // 用于检查静态任务是否创建成功的句柄。
    g_app_ir_status = APP_IR_PENDING;
    g_app_ir_events = 0U;
    g_app_ir_raw_code = 0U;
    g_app_ir_address = 0U;
    g_app_ir_command = 0U;
    g_app_ir_repeat = 0U;
    task = xTaskCreateStatic(ir_task, "ir_remote", APP_IR_TASK_STACK_WORDS, NULL,
        APP_IR_TASK_PRIORITY, s_ir_task_stack, &s_ir_task_tcb);
    if (task == NULL) { g_app_ir_status = APP_IR_ERROR; return 0U; }
    return 1U;
}
