#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "app_ultrasonic_range.h"

static const DevUltrasonicRangeConfig s_config = {
    APP_ULTRASONIC_RANGE_BURST_CYCLES,
    APP_ULTRASONIC_RANGE_THRESHOLD_CODE,
    APP_ULTRASONIC_RANGE_BLANKING_US,
    APP_ULTRASONIC_RANGE_TIMEOUT_US,
    APP_ULTRASONIC_RANGE_MAX_DISTANCE_MM,
    APP_ULTRASONIC_RANGE_SOUND_SPEED_MM_S,
    APP_ULTRASONIC_RANGE_TIME_OFFSET_US,
    APP_ULTRASONIC_RANGE_RECEIVE_ONLY_TEST
}; // 第1路首版参数集中定义，调门限或消隐后需重新构建验证。
volatile DevUltrasonicRangeResult g_ultrasonic_range_result = {0}; // APP发布的最新距离及有效状态。
volatile DevUltrasonicRangeResult g_ultrasonic_range_last_result = {0}; // 保存最近完成的一轮；新测量、停止和重启不覆盖历史记录。
volatile uint32_t g_ultrasonic_range_completed_count; // 自上电累计完成或失败的测量次数，零表示尚无完成记录。
volatile uint32_t g_ultrasonic_range_valid_count; // 自上电累计取得有效距离的测量次数。
volatile uint32_t g_ultrasonic_range_no_echo_count; // 自上电累计接收截止仍无合格回波的测量次数。
static DevUltrasonicRange *s_device; // 只在显式初始化成功后保留的设备接口。
static StaticTask_t s_task_storage; // 测距静态任务控制块。
static StackType_t s_task_stack[APP_ULTRASONIC_RANGE_STACK_WORDS]; // 测距任务栈，单位为32位字。
static StaticTimer_t s_timer_storage; // 周期测量静态软件定时器存储。
static TaskHandle_t s_task; // 接收100ms周期通知的任务句柄。
static TimerHandle_t s_timer; // 周期通知定时器句柄。
static volatile bool s_enabled = false; // 周期业务是否允许启动新一轮测量。
static volatile uint32_t s_generation; // 停止/重启时取消旧任务循环的结果发布。

static void ultrasonic_range_task(void *argument);
static void ultrasonic_range_timer_callback(TimerHandle_t timer);
static void app_ultrasonic_range_publish(const DevUltrasonicRangeResult *result);

/** @brief 在短临界区发布实时快照，保存最近完成结果并累计一次测量的终态。
 * @param result 要发布的非空完整结果。 */
static void app_ultrasonic_range_publish(const DevUltrasonicRangeResult *result)
{
    const bool completed = result->state == DEV_ULTRASONIC_RANGE_VALID ||
        result->state == DEV_ULTRASONIC_RANGE_NO_ECHO ||
        result->state == DEV_ULTRASONIC_RANGE_OUT_OF_RANGE ||
        result->state == DEV_ULTRASONIC_RANGE_CAPTURE_OVERFLOW ||
        result->state == DEV_ULTRASONIC_RANGE_IO_ERROR; // 完成和失败均记录；进行中及人为停止不冒充完成测量。
    taskENTER_CRITICAL();
    g_ultrasonic_range_result = *result;
    if (completed && (g_ultrasonic_range_completed_count == 0UL ||
        result->sequence != g_ultrasonic_range_last_result.sequence)) {
        /* 同一序号的终态只计一次；新一轮实时清零不会擦掉上一轮的距离或失败原因。 */
        g_ultrasonic_range_last_result = *result;
        ++g_ultrasonic_range_completed_count;
        if (result->valid) ++g_ultrasonic_range_valid_count;
        if (result->state == DEV_ULTRASONIC_RANGE_NO_ECHO) ++g_ultrasonic_range_no_echo_count;
    }
    taskEXIT_CRITICAL();
}

/** @brief 周期回调仅通知任务，不唤醒驱动、不阻塞或分配内存。
 * @param timer 发起回调的静态软件定时器。 */
static void ultrasonic_range_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    if (s_enabled && s_task != NULL) xTaskNotifyGive(s_task);
}

/** @brief 等待启动通知后每1ms查询结果；只接收持续监听，测距按周期通知发射。
 * @param argument 保留任务参数，当前不使用。 */
static void ultrasonic_range_task(void *argument)
{
    DevUltrasonicRangeResult result = {0}; // 单次测量读取结果，失败时显式置无效。
    DevStatus status; // 当前设备开始或查询结果。
    TickType_t started_tick; // 看门超时只防外设/IRQ不推进，不用它计算距离。
    uint32_t generation; // 本轮任务使用的业务代号，Stop/Start取消旧发布。
    const TickType_t query_ticks = pdMS_TO_TICKS(APP_ULTRASONIC_RANGE_QUERY_MS); // 查询期间阻塞节拍。
    const TickType_t watchdog_ticks = pdMS_TO_TICKS(APP_ULTRASONIC_RANGE_TIMEOUT_US /
        APP_ULTRASONIC_RANGE_US_PER_MS + APP_ULTRASONIC_RANGE_WATCHDOG_MARGIN_MS); // 硬件截止后的有限兜底。
    (void)argument;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!s_enabled) continue;
        generation = s_generation;
        status = s_device->startMeasurement(s_device);
        taskENTER_CRITICAL();
        if (!s_enabled || generation != s_generation) {
            taskEXIT_CRITICAL();
            continue;
        }
        if (status != DEV_OK && status != DEV_PENDING) {
            (void)s_device->stop(s_device);
            (void)s_device->readResult(s_device, &result);
            result.distance_mm = 0UL;
            result.valid = false;
            result.state = DEV_ULTRASONIC_RANGE_IO_ERROR;
            app_ultrasonic_range_publish(&result);
            s_enabled = false; /* 板级启动失败不反复重新唤醒，等待明确重启。 */
            taskEXIT_CRITICAL();
            continue;
        }
        taskEXIT_CRITICAL();
        started_tick = xTaskGetTickCount();
        do {
            /* 查询、代号检查和发布作为同一短临界区，Stop之后不发布旧的有效结果。 */
            taskENTER_CRITICAL();
            status = s_device->readResult(s_device, &result);
            if (!s_enabled || generation != s_generation) {
                taskEXIT_CRITICAL();
                break;
            }
            if (status != DEV_OK && status != DEV_PENDING) {
                (void)s_device->stop(s_device);
                result.distance_mm = 0UL;
                result.valid = false;
                result.state = DEV_ULTRASONIC_RANGE_IO_ERROR;
                app_ultrasonic_range_publish(&result);
                s_enabled = false;
                taskEXIT_CRITICAL();
                break;
            }
            app_ultrasonic_range_publish(&result);
            if (status == DEV_OK) {
                if (result.state == DEV_ULTRASONIC_RANGE_IO_ERROR) s_enabled = false;
                taskEXIT_CRITICAL();
                break;
            }
            if (!s_config.receive_only && (TickType_t)(xTaskGetTickCount() - started_tick) >= watchdog_ticks) {
                (void)s_device->stop(s_device);
                result.distance_mm = 0UL;
                result.echo_time_us = 0UL;
                result.valid = false;
                result.state = DEV_ULTRASONIC_RANGE_IO_ERROR;
                app_ultrasonic_range_publish(&result);
                s_enabled = false;
                taskEXIT_CRITICAL();
                break;
            }
            taskEXIT_CRITICAL();
            vTaskDelay(query_ticks);
        } while (s_enabled && generation == s_generation);
    }
}

/** @brief 获取并显式初始化设备，复用静态任务；只接收通知一次，测距启动周期通知。
 * @return 成功DEV_OK，资源/命令失败DEV_IO_ERROR，其他返回设备原错误。 */
DevStatus APP_UltrasonicRange_Start(void)
{
    DevUltrasonicRange *device = GetUltrasonicRange(); // 获取接口不操作硬件。
    DevStatus status; // 显式初始化或结果查询状态。
    DevUltrasonicRangeResult result; // 初始化后的完整无效结果。
    const TickType_t period_ticks = pdMS_TO_TICKS(APP_ULTRASONIC_RANGE_PERIOD_MS); // 周期通知间隔。
    if (s_enabled) return DEV_OK;
    if (device == NULL) return DEV_NOT_READY;
    if (period_ticks == 0U || pdMS_TO_TICKS(APP_ULTRASONIC_RANGE_QUERY_MS) == 0U) return DEV_IO_ERROR;
    status = device->init(device, &s_config);
    if (status != DEV_OK) return status;
    s_device = device;
    /* 新建对象前已完成设备配置；重复启动只重用原静态存储。 */
    if (s_task == NULL) {
        s_task = xTaskCreateStatic(ultrasonic_range_task, "ultrasonic", APP_ULTRASONIC_RANGE_STACK_WORDS,
                                  NULL, APP_ULTRASONIC_RANGE_TASK_PRIORITY, s_task_stack, &s_task_storage);
        if (s_task == NULL) {
            (void)device->stop(device);
            return DEV_IO_ERROR;
        }
    }
    if (!s_config.receive_only && s_timer == NULL) {
        s_timer = xTimerCreateStatic("ultrasonic_timer", period_ticks, pdTRUE, NULL,
                                    ultrasonic_range_timer_callback, &s_timer_storage);
        if (s_timer == NULL) {
            (void)device->stop(device);
            return DEV_IO_ERROR;
        }
    }
    status = device->readResult(device, &result);
    if (status != DEV_OK) {
        (void)device->stop(device);
        return status;
    }
    result.distance_mm = 0UL;
    result.valid = false;
    taskENTER_CRITICAL();
    ++s_generation;
    s_enabled = true;
    g_ultrasonic_range_result = result;
    taskEXIT_CRITICAL();
    /* 启动前零等待入队；回调本身只通知任务，驱动唤醒在任务中执行。 */
    if (!s_config.receive_only && xTimerStart(s_timer, APP_ULTRASONIC_RANGE_COMMAND_WAIT_TICKS) != pdPASS) {
        (void)APP_UltrasonicRange_Stop();
        return DEV_IO_ERROR;
    }
    xTaskNotifyGive(s_task); /* 首次测量或持续接收由任务在调度器内执行。 */
    return DEV_OK;
}

/** @brief 先禁止新测量，再停止周期通知和设备；重复停止保持无效状态。
 * @return 成功DEV_OK，未建立设备DEV_NOT_READY，命令/关闭失败DEV_IO_ERROR。 */
DevStatus APP_UltrasonicRange_Stop(void)
{
    DevStatus status; // 设备关闭状态。
    DevUltrasonicRangeResult result; // 用于发布STOPPED或关闭故障状态。
    bool timer_stopped = true; // 定时器命令失败仍禁止任务继续触发。
    taskENTER_CRITICAL();
    s_enabled = false;
    ++s_generation;
    taskEXIT_CRITICAL();
    if (s_timer != NULL)
        timer_stopped = xTimerStop(s_timer, APP_ULTRASONIC_RANGE_COMMAND_WAIT_TICKS) == pdPASS;
    if (s_device == NULL) return DEV_NOT_READY;
    status = s_device->stop(s_device);
    if (s_device->readResult(s_device, &result) == DEV_OK) {
        result.distance_mm = 0UL;
        result.valid = false;
        if (!timer_stopped) result.state = DEV_ULTRASONIC_RANGE_IO_ERROR;
        app_ultrasonic_range_publish(&result);
    }
    return timer_stopped ? status : DEV_IO_ERROR;
}
