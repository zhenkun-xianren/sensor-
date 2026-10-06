#include "app_measurement.h"
#include "dev_adc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <stddef.h>

static DevAdc *s_adc; // 保存由测量任务独占操作的ADC Device对象。
static bool s_initialized = false; // 保存测量模块启动前初始化是否完成。
static bool s_started = false; // 防止重复创建任务和定时器。
static bool s_reinit_required = false; // 保存ADC失败后下一周期需要重新校准的状态。
static AppMeasurementSnapshot s_snapshot; // 保存只通过短临界区读写的完整测量快照。
static TickType_t s_snapshot_tick; // 保存快照更新的原始Tick，供回绕安全的新鲜度判断。
static StaticTask_t s_task_control; // 保存静态采样任务控制块。
static StackType_t s_task_stack[APP_MEASUREMENT_TASK_STACK_WORDS]; // 保存采样任务栈，容量以字为单位。
static TaskHandle_t s_task; // 保存采样任务句柄，供软件定时器通知。
static StaticTimer_t s_timer_control; // 保存静态采样定时器控制块。
static TimerHandle_t s_timer; // 保存每100ms通知一次的定时器句柄。
volatile AppMeasurementStatus g_measurement_status = APP_MEASUREMENT_NOT_READY; // 保存最近周期业务状态。
volatile DevStatus g_measurement_adc_status = DEV_NOT_READY; // 保存最近ADC操作状态。
volatile uint32_t g_measurement_update_count; // 统计测量尝试次数，包含失败。
volatile uint32_t g_measurement_adc_error_count; // 累计运行期ADC失败周期，成功后不清除。
volatile DevStatus g_measurement_last_adc_error = DEV_OK; // 保留最近失败原因，结合失败次数判断。
volatile AppMeasurementSnapshot g_measurement_last_valid; // 保留最近成功原始码和毫伏值，仅供调试。

/**
 * @brief 软件定时器仅发送采样通知，不执行ADC等待、换算或显示操作。
 * @param timer 本模块静态软件定时器句柄。
 */
static void measurement_notify_callback(TimerHandle_t timer)
{
    (void)timer;
    (void)xTaskNotifyGive(s_task);
}

/**
 * @brief 串行采集32组原始码，以两路累加值的比值还原外部输入电压。
 * @param output 接收本周期数据及错误状态的非空快照对象。
 */
static void measurement_collect(AppMeasurementSnapshot *output)
{
    DevAdcRawPair pair; // 保存本次成对读取的两路原始码。
    uint32_t signal_sum = 0UL; // 保存测量通道32次完整采样的累加值。
    uint32_t reference_sum = 0UL; // 保存参考通道32次完整采样的累加值。
    uint32_t index; // 遍历本周期需要采集的数据对。
    bool reference_invalid = false; // 记录任一次参考为零或饱和。
    bool signal_saturated = false; // 记录任一次测量信号触及ADC上下电源轨。
    int64_t numerator; // 保存带偏置扣除的有符号64位换算分子。
    int64_t denominator; // 保存由参考累加值推导的换算分母。
    int64_t converted; // 保存四舍五入后、边界处理前的毫伏结果。
    if (s_reinit_required) {
        output->adc_status = s_adc->init(s_adc);
        if (output->adc_status != DEV_OK) {
            output->status = APP_MEASUREMENT_ADC_ERROR;
            return;
        }
        s_reinit_required = false;
    }
    for (index = 0UL; index < APP_MEASUREMENT_SAMPLE_COUNT; ++index) {
        output->adc_status = s_adc->readPair(s_adc, &pair);
        if (output->adc_status != DEV_OK) {
            output->status = APP_MEASUREMENT_ADC_ERROR;
            s_reinit_required = true;
            return; // 不把不完整采样序列写成32次平均或有效电压。
        }
        signal_sum += pair.signal_raw;
        reference_sum += pair.reference_raw;
        reference_invalid = reference_invalid || pair.reference_raw < APP_MEASUREMENT_REFERENCE_MIN_RAW ||
            pair.reference_raw >= DEV_ADC_RAW_MAX;
        signal_saturated = signal_saturated || pair.signal_raw < APP_MEASUREMENT_SIGNAL_MIN_RAW ||
            pair.signal_raw >= DEV_ADC_RAW_MAX;
    }
    output->signal_raw = (uint16_t)((signal_sum + APP_MEASUREMENT_SAMPLE_COUNT / APP_MEASUREMENT_ROUND_DIVISOR) /
        APP_MEASUREMENT_SAMPLE_COUNT);
    output->reference_raw = (uint16_t)((reference_sum + APP_MEASUREMENT_SAMPLE_COUNT / APP_MEASUREMENT_ROUND_DIVISOR) /
        APP_MEASUREMENT_SAMPLE_COUNT);
    if (reference_invalid) {
        output->status = APP_MEASUREMENT_REFERENCE_ERROR;
        return;
    }
    if (signal_saturated) {
        output->status = APP_MEASUREMENT_OUT_OF_RANGE;
        return;
    }
    /* 分压偏置关系：(30*Vin+3*Vref)/53；使用累加比值避免先平均截断。 */
    numerator = APP_MEASUREMENT_REFERENCE_MV *
        (APP_MEASUREMENT_SIGNAL_FACTOR * signal_sum - APP_MEASUREMENT_BIAS_FACTOR * reference_sum);
    denominator = APP_MEASUREMENT_INPUT_FACTOR * reference_sum;
    converted = (numerator >= 0LL ? numerator + denominator / APP_MEASUREMENT_ROUND_DIVISOR :
        numerator - denominator / APP_MEASUREMENT_ROUND_DIVISOR) / denominator;
    output->unclamped_millivolts = (int32_t)converted;
    if (converted < APP_MEASUREMENT_INPUT_MIN_MV - APP_MEASUREMENT_BOUNDARY_TOLERANCE_MV ||
        converted > APP_MEASUREMENT_INPUT_MAX_MV + APP_MEASUREMENT_BOUNDARY_TOLERANCE_MV) {
        output->status = APP_MEASUREMENT_OUT_OF_RANGE;
        return;
    }
    if (converted < APP_MEASUREMENT_INPUT_MIN_MV) {
        output->millivolts = (uint16_t)APP_MEASUREMENT_INPUT_MIN_MV;
        output->boundary_clamped = true;
    } else if (converted > APP_MEASUREMENT_INPUT_MAX_MV) {
        output->millivolts = (uint16_t)APP_MEASUREMENT_INPUT_MAX_MV;
        output->boundary_clamped = true;
    } else {
        output->millivolts = (uint16_t)converted;
    }
    output->status = APP_MEASUREMENT_OK;
}

/**
 * @brief 等待周期通知，完成采集并原子发布完整结果；失败也发布失效状态。
 * @param argument 未使用的静态任务参数。
 */
static void measurement_task(void *argument)
{
    (void)argument;
    for (;;) {
        AppMeasurementSnapshot next = {0}; // 保存本周期待发布快照，避免遗留上次有效值。
        TickType_t completed_tick; // 保存本周期尝试完成时的原始Tick。
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == APP_MEASUREMENT_NO_NOTIFICATION) continue;
        next.status = APP_MEASUREMENT_NOT_READY;
        next.adc_status = DEV_NOT_READY;
        next.boundary_clamped = false;
        measurement_collect(&next);
        completed_tick = xTaskGetTickCount();
        next.timestamp_ms = (uint32_t)((uint64_t)completed_tick * APP_MEASUREMENT_MILLISECONDS_PER_SECOND /
            configTICK_RATE_HZ);
        /* 只复制小快照；临界区中不进行采样、换算、显示或通信。 */
        taskENTER_CRITICAL();
        s_snapshot = next;
        s_snapshot_tick = completed_tick;
        if (next.status == APP_MEASUREMENT_OK) {
            g_measurement_last_valid = next;
        } else if (next.status == APP_MEASUREMENT_ADC_ERROR) {
            ++g_measurement_adc_error_count;
            g_measurement_last_adc_error = next.adc_status;
        }
        taskEXIT_CRITICAL();
        g_measurement_status = next.status;
        g_measurement_adc_status = next.adc_status;
        ++g_measurement_update_count;
    }
}

/**
 * @brief 调度器启动前初始化ADC Device，并建立尚无有效数据的快照。
 * @return 成功APP_OK，ADC失败或运行期重复初始化APP_ERROR_MEASUREMENT。
 */
AppStatus App_Measurement_Init(void)
{
    AppMeasurementSnapshot initial = {0}; // 保存测量开始前的未就绪快照。
    if (s_started) return APP_ERROR_MEASUREMENT;
    s_initialized = false;
    initial.status = APP_MEASUREMENT_NOT_READY;
    initial.adc_status = DEV_NOT_READY;
    initial.boundary_clamped = false;
    s_snapshot = initial; // 此接口只在调度器启动前调用，不与运行任务并发。
    g_measurement_last_valid = initial; // 首次成功前的历史诊断也必须标记未就绪。
    s_adc = GetAdc();
    if (s_adc == NULL || s_adc->init == NULL || s_adc->readPair == NULL) return APP_ERROR_MEASUREMENT;
    g_measurement_adc_status = s_adc->init(s_adc);
    if (g_measurement_adc_status != DEV_OK) {
        s_snapshot.status = APP_MEASUREMENT_ADC_ERROR;
        s_snapshot.adc_status = g_measurement_adc_status;
        g_measurement_status = APP_MEASUREMENT_ADC_ERROR;
        return APP_ERROR_MEASUREMENT;
    }
    s_reinit_required = false;
    s_snapshot.adc_status = g_measurement_adc_status;
    s_initialized = true;
    g_measurement_status = APP_MEASUREMENT_NOT_READY;
    return APP_OK;
}

/**
 * @brief 静态创建测量任务及100ms定时器，不占用硬件定时器或动态堆。
 * @return 成功APP_OK，未初始化APP_ERROR_MEASUREMENT，任务失败APP_ERROR_TASK，定时器失败APP_ERROR_TIMER。
 */
AppStatus App_Measurement_Start(void)
{
    if (!s_initialized) return APP_ERROR_MEASUREMENT;
    if (s_started) return APP_OK;
    s_task = xTaskCreateStatic(measurement_task, "measurement", APP_MEASUREMENT_TASK_STACK_WORDS,
        NULL, APP_MEASUREMENT_TASK_PRIORITY, s_task_stack, &s_task_control);
    if (s_task == NULL) return APP_ERROR_TASK;
    s_timer = xTimerCreateStatic("adc_sample", pdMS_TO_TICKS(APP_MEASUREMENT_PERIOD_MS), pdTRUE,
        NULL, measurement_notify_callback, &s_timer_control);
    if (s_timer == NULL || xTimerStart(s_timer, APP_MEASUREMENT_COMMAND_WAIT_TICKS) != pdPASS)
        return APP_ERROR_TIMER;
    s_started = true;
    return APP_OK;
}

/**
 * @brief 复制完整测量快照并检查回绕安全的数据有效期限。
 * @param output 非空快照输出指针；错误状态时仍提供完整诊断字段。
 * @return 最新测量状态，过期返回STALE，未初始化返回NOT_READY，空指针返回ARGUMENT_ERROR。
 */
AppMeasurementStatus App_Measurement_GetLatest(AppMeasurementSnapshot *output)
{
    AppMeasurementSnapshot latest; // 保存临界区内复制的整组数据。
    TickType_t updated_tick; // 保存与该快照对应的原始更新时间。
    TickType_t now_tick; // 保存读取时的原始Tick。
    if (output == NULL) return APP_MEASUREMENT_ARGUMENT_ERROR;
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        /* 启动前没有并发采样任务，避免触碰FreeRTOS尚未复位的临界区嵌套状态。 */
        latest = s_snapshot;
        updated_tick = s_snapshot_tick;
    } else {
        taskENTER_CRITICAL();
        latest = s_snapshot;
        updated_tick = s_snapshot_tick;
        taskEXIT_CRITICAL();
    }
    now_tick = xTaskGetTickCount();
    if (!s_initialized && latest.status != APP_MEASUREMENT_ADC_ERROR) {
        latest.status = APP_MEASUREMENT_NOT_READY;
        latest.adc_status = DEV_NOT_READY;
    } else if (latest.status == APP_MEASUREMENT_OK &&
        (TickType_t)(now_tick - updated_tick) > pdMS_TO_TICKS(APP_MEASUREMENT_STALE_MS)) {
        latest.status = APP_MEASUREMENT_STALE;
    }
    *output = latest;
    return latest.status;
}
