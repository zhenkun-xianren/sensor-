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
    APP_ULTRASONIC_RANGE_RECEIVE_ONLY_TEST,
    APP_ULTRASONIC_RANGE_MIN_DISTANCE_MM
}; // 第1路首版参数集中定义，调门限或消隐后需重新构建验证。
volatile DevUltrasonicRangeResult g_ultrasonic_range_result = {0}; // APP发布的最新距离及有效状态。
volatile DevUltrasonicRangeResult g_ultrasonic_range_last_result = {0}; // 保存最近完成的一轮；新测量、停止和重启不覆盖历史记录。
volatile uint32_t g_ultrasonic_range_completed_count; // 自上电累计完成或失败的测量次数，零表示尚无完成记录。
volatile uint32_t g_ultrasonic_range_valid_count; // 自上电累计取得有效距离的测量次数。
volatile uint32_t g_ultrasonic_range_valid_buffer[APP_ULTRASONIC_RANGE_VALID_BUFFER_CAPACITY] = {0}; // 合格毫米距离循环历史，数组值需结合有效数量读取。
volatile uint32_t g_ultrasonic_range_valid_buffer_next_index; // 下一写入位置，满后覆盖最旧项；只由发布函数更新。
volatile uint32_t g_ultrasonic_range_valid_buffer_count; // 已保存的有效项数，上限为数组容量，测量停止/重启不清除。
volatile uint32_t g_ultrasonic_range_no_echo_count; // 自上电累计接收截止仍无合格回波的测量次数。
volatile AppUltrasonicRangeFilteredResult g_ultrasonic_range_filtered_result = {0}; // 保存稳定距离、有效期和筛选统计，供APP或Watch读取。
static uint32_t s_filter_samples[APP_ULTRASONIC_RANGE_FILTER_SAMPLES]; // 当前一致候选簇的最近三个有效距离。
static uint32_t s_filter_sample_count; // 候选簇已取得的有效样本数，最多为三个。
static uint32_t s_filter_next_slot; // 环形候选缓冲区下一次写入的位置。
static uint32_t s_filter_last_sample_mm; // 相邻有效样本的一致性比较基准。
static TickType_t s_filter_last_sample_tick; // 最近候选样本到达时刻，防止旧候选跨长时间拼接。
static TickType_t s_filter_accepted_tick; // 最近可信更新的时刻，500ms后明确失效。
static bool s_filter_has_accepted = false; // 是否取得过可信更新，用于区分未确认和数据过期。
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

/** @brief 保留原始快照，将新合格距离写入循环历史，并发布距离中值和有效期。
 * @param result 要发布的非空完整原始结果；不把稳定结果写回原始距离。 */
static void app_ultrasonic_range_publish(const DevUltrasonicRangeResult *result)
{
    const bool completed = result->state == DEV_ULTRASONIC_RANGE_VALID ||
        result->state == DEV_ULTRASONIC_RANGE_NO_ECHO ||
        result->state == DEV_ULTRASONIC_RANGE_OUT_OF_RANGE ||
        result->state == DEV_ULTRASONIC_RANGE_CAPTURE_OVERFLOW ||
        result->state == DEV_ULTRASONIC_RANGE_IO_ERROR; // 只有完成或失败状态才能计入多轮筛选。
    const TickType_t now = xTaskGetTickCount(); // 当前任务节拍，只用于结果有效期，不参与回波计时。
    const TickType_t expiry_ticks = pdMS_TO_TICKS(APP_ULTRASONIC_RANGE_FILTER_EXPIRE_MS); // 稳定结果与候选样本的有效期。
    TickType_t age_ticks; // 距上一次可信更新的节拍差，自然回绕。
    uint32_t tolerance_mm; // 一致性容差为100mm与相邻距离百分比中的较大值。
    uint32_t difference_mm; // 相邻有效样本的绝对距离差。
    uint32_t sorted[APP_ULTRASONIC_RANGE_FILTER_SAMPLES]; // 三点中值排序副本，不改变环形缓冲区。
    uint32_t index; // 复制及排序时使用的数学遍历索引。
    uint32_t compare_index; // 与当前排序项比较的后续索引。
    uint32_t temporary_mm; // 交换两个排序项时保存的距离。
    bool new_completed; // 同一终态序号只处理一次，避免1ms查询重复进入筛选。
    taskENTER_CRITICAL();
    g_ultrasonic_range_result = *result;
    new_completed = completed && (g_ultrasonic_range_completed_count == 0UL ||
        result->sequence != g_ultrasonic_range_last_result.sequence);
    if (new_completed) {
        g_ultrasonic_range_last_result = *result;
        ++g_ultrasonic_range_completed_count;
        if (result->valid) {
            ++g_ultrasonic_range_valid_count;
            /* 只保存新完成的合格轮次；当前设备已排除大于2000mm的结果。
             * 临界区内先写数据再发布索引/数量，满后自然覆盖最旧项。 */
            g_ultrasonic_range_valid_buffer[g_ultrasonic_range_valid_buffer_next_index] = result->distance_mm;
            ++g_ultrasonic_range_valid_buffer_next_index;
            if (g_ultrasonic_range_valid_buffer_next_index >= APP_ULTRASONIC_RANGE_VALID_BUFFER_CAPACITY)
                g_ultrasonic_range_valid_buffer_next_index = 0UL;
            if (g_ultrasonic_range_valid_buffer_count < APP_ULTRASONIC_RANGE_VALID_BUFFER_CAPACITY)
                ++g_ultrasonic_range_valid_buffer_count;
        } else ++g_ultrasonic_range_filtered_result.invalid_measurements;
        if (result->state == DEV_ULTRASONIC_RANGE_NO_ECHO) ++g_ultrasonic_range_no_echo_count;
    }
    if (s_config.receive_only || result->state == DEV_ULTRASONIC_RANGE_IDLE ||
        result->state == DEV_ULTRASONIC_RANGE_STOPPED || result->state == DEV_ULTRASONIC_RANGE_IO_ERROR) {
        /* 停止、硬件错误与只接收不保留一个看似有效的历史距离；统计仍保留。 */
        g_ultrasonic_range_filtered_result.distance_mm = 0UL;
        g_ultrasonic_range_filtered_result.age_ms = 0UL;
        g_ultrasonic_range_filtered_result.sequence = 0UL;
        g_ultrasonic_range_filtered_result.pending_samples = 0UL;
        g_ultrasonic_range_filtered_result.valid = false;
        g_ultrasonic_range_filtered_result.stale = false;
        s_filter_has_accepted = false;
        s_filter_sample_count = 0UL;
        s_filter_next_slot = 0UL;
        taskEXIT_CRITICAL();
        return;
    }
    if (s_filter_has_accepted) {
        age_ticks = (TickType_t)(now - s_filter_accepted_tick);
        g_ultrasonic_range_filtered_result.age_ms = (uint32_t)age_ticks * portTICK_PERIOD_MS;
        if (g_ultrasonic_range_filtered_result.valid && age_ticks >= expiry_ticks) {
            /* 持续丢波不能永久冻结旧值；过期后必须重新取得三个一致样本。 */
            g_ultrasonic_range_filtered_result.valid = false;
            g_ultrasonic_range_filtered_result.stale = true;
            g_ultrasonic_range_filtered_result.distance_mm = 0UL;
            s_filter_sample_count = 0UL;
            s_filter_next_slot = 0UL;
        }
    }
    if (s_filter_sample_count != 0UL && (TickType_t)(now - s_filter_last_sample_tick) >= expiry_ticks) {
        s_filter_sample_count = 0UL;
        s_filter_next_slot = 0UL;
    }
    if (new_completed) {
        if (result->valid && result->state == DEV_ULTRASONIC_RANGE_VALID && result->distance_mm != 0UL) {
            if (s_filter_sample_count != 0UL) {
                tolerance_mm = s_filter_last_sample_mm * APP_ULTRASONIC_RANGE_FILTER_RELATIVE_PERCENT /
                    APP_ULTRASONIC_RANGE_FILTER_PERCENT_SCALE;
                if (tolerance_mm < APP_ULTRASONIC_RANGE_FILTER_MIN_TOLERANCE_MM)
                    tolerance_mm = APP_ULTRASONIC_RANGE_FILTER_MIN_TOLERANCE_MM;
                difference_mm = result->distance_mm >= s_filter_last_sample_mm ?
                    result->distance_mm - s_filter_last_sample_mm : s_filter_last_sample_mm - result->distance_mm;
                if (difference_mm > tolerance_mm) {
                    /* 大跳变建立新候选簇，三次一致后可跟踪真实移动，不锁死在某个距离。 */
                    ++g_ultrasonic_range_filtered_result.outlier_count;
                    s_filter_sample_count = 0UL;
                    s_filter_next_slot = 0UL;
                }
            }
            s_filter_samples[s_filter_next_slot] = result->distance_mm;
            ++s_filter_next_slot;
            if (s_filter_next_slot >= APP_ULTRASONIC_RANGE_FILTER_SAMPLES) s_filter_next_slot = 0UL;
            if (s_filter_sample_count < APP_ULTRASONIC_RANGE_FILTER_SAMPLES) ++s_filter_sample_count;
            s_filter_last_sample_mm = result->distance_mm;
            s_filter_last_sample_tick = now;
            if (s_filter_sample_count == APP_ULTRASONIC_RANGE_FILTER_SAMPLES) {
                for (index = 0UL; index < APP_ULTRASONIC_RANGE_FILTER_SAMPLES; ++index)
                    sorted[index] = s_filter_samples[index];
                for (index = 0UL; index < APP_ULTRASONIC_RANGE_FILTER_SAMPLES; ++index) {
                    for (compare_index = index + 1UL; compare_index < APP_ULTRASONIC_RANGE_FILTER_SAMPLES; ++compare_index) {
                        if (sorted[compare_index] < sorted[index]) {
                            temporary_mm = sorted[index];
                            sorted[index] = sorted[compare_index];
                            sorted[compare_index] = temporary_mm;
                        }
                    }
                }
                g_ultrasonic_range_filtered_result.distance_mm = sorted[APP_ULTRASONIC_RANGE_FILTER_MEDIAN_INDEX];
                g_ultrasonic_range_filtered_result.sequence = result->sequence;
                g_ultrasonic_range_filtered_result.age_ms = 0UL;
                g_ultrasonic_range_filtered_result.valid = true;
                g_ultrasonic_range_filtered_result.stale = false;
                ++g_ultrasonic_range_filtered_result.accepted_updates;
                s_filter_accepted_tick = now;
                s_filter_has_accepted = true;
            } else if (g_ultrasonic_range_filtered_result.valid) {
                g_ultrasonic_range_filtered_result.stale = true;
            }
        } else if (g_ultrasonic_range_filtered_result.valid) {
            g_ultrasonic_range_filtered_result.stale = true;
        }
    }
    g_ultrasonic_range_filtered_result.pending_samples = s_filter_sample_count;
    taskEXIT_CRITICAL();
}

/** @brief 周期回调仅通知任务，不唤醒驱动、不阻塞或分配内存。
 * @param timer 发起回调的静态软件定时器。 */
static void ultrasonic_range_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    if (s_enabled && s_task != NULL) xTaskNotifyGive(s_task);
}

/** @brief 按通知启动测量，每1ms查询及更新稳定结果有效期；停止时阻塞等待。
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
        /* 空闲测量间隔也每1ms更新有效期，不能靠下一次回波才让旧距离失效。 */
        if (ulTaskNotifyTake(pdTRUE, s_enabled ? query_ticks : portMAX_DELAY) == 0UL) {
            taskENTER_CRITICAL();
            if (s_enabled) {
                result = g_ultrasonic_range_result;
                app_ultrasonic_range_publish(&result); /* 重复终态序号不重复统计或加入样本。 */
            }
            taskEXIT_CRITICAL();
            continue;
        }
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

/** @brief 显式初始化并清除候选；关门测试保持初始化钳位，其他模式建立任务和所需通知。
 * @return 成功DEV_OK，资源/命令失败DEV_IO_ERROR，其他返回设备原错误。 */
DevStatus APP_UltrasonicRange_Start(void)
{
    DevUltrasonicRange *device = GetUltrasonicRange(); // 获取接口不操作硬件。
    DevStatus status; // 显式初始化或结果查询状态。
    DevUltrasonicRangeResult result; // 初始化后的完整无效结果。
    const TickType_t period_ticks = pdMS_TO_TICKS(APP_ULTRASONIC_RANGE_PERIOD_MS); // 周期通知间隔。
    if (s_enabled) return DEV_OK;
    if (device == NULL) return DEV_NOT_READY;
    if (period_ticks == 0U || pdMS_TO_TICKS(APP_ULTRASONIC_RANGE_QUERY_MS) == 0U ||
        pdMS_TO_TICKS(APP_ULTRASONIC_RANGE_FILTER_EXPIRE_MS) == 0U) return DEV_IO_ERROR;
    status = device->init(device, &s_config);
    if (status != DEV_OK) return status;
    s_device = device;
    if (APP_ULTRASONIC_RANGE_HOLD_CLAMP_TEST) {
        /* 初始化已关闭全部发射并钳位接收；此处禁止创建任务或通知，保持PA4高。
         * COMP2/DAC已配置，示波器可观察PA3；捕获未启动，计数和距离保持无效。 */
        status = device->readResult(device, &result);
        if (status != DEV_OK) {
            (void)device->stop(device);
            return status;
        }
        result.distance_mm = 0UL;
        result.valid = false;
        taskENTER_CRITICAL();
        ++s_generation;
        app_ultrasonic_range_publish(&result);
        taskEXIT_CRITICAL();
        return DEV_OK;
    } else {
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
        app_ultrasonic_range_publish(&result); /* 初始化或停止态清除筛选历史，原始完成记录仍保留。 */
        taskEXIT_CRITICAL();
        /* 启动前零等待入队；回调本身只通知任务，驱动唤醒在任务中执行。 */
        if (!s_config.receive_only && xTimerStart(s_timer, APP_ULTRASONIC_RANGE_COMMAND_WAIT_TICKS) != pdPASS) {
            (void)APP_UltrasonicRange_Stop();
            return DEV_IO_ERROR;
        }
        xTaskNotifyGive(s_task); /* 首次测量或持续接收由任务在调度器内执行。 */
        return DEV_OK;
    }
}

/** @brief 先禁止新测量并撤销稳定距离，再停止周期通知和设备；重复停止保持无效。
 * @return 成功DEV_OK，未建立设备DEV_NOT_READY，命令/关闭失败DEV_IO_ERROR。 */
DevStatus APP_UltrasonicRange_Stop(void)
{
    DevStatus status; // 设备关闭状态。
    DevUltrasonicRangeResult result; // 用于发布STOPPED或关闭故障状态。
    bool timer_stopped = true; // 定时器命令失败仍禁止任务继续触发。
    taskENTER_CRITICAL();
    s_enabled = false;
    ++s_generation;
    /* 即使后续设备查询失败，也不向外保留有效距离。 */
    g_ultrasonic_range_filtered_result.distance_mm = 0UL;
    g_ultrasonic_range_filtered_result.age_ms = 0UL;
    g_ultrasonic_range_filtered_result.sequence = 0UL;
    g_ultrasonic_range_filtered_result.pending_samples = 0UL;
    g_ultrasonic_range_filtered_result.valid = false;
    g_ultrasonic_range_filtered_result.stale = false;
    s_filter_has_accepted = false;
    s_filter_sample_count = 0UL;
    s_filter_next_slot = 0UL;
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
