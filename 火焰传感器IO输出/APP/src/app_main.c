#include <stddef.h>
#include "app_main.h"
#include "app_flame_config.h"

enum {
    APP_REPORT_PERIOD_MS = 1000, APP_REPORT_MIN_INTERVAL_MS = 100,
    APP_COMMUNICATION_RETRY_MS = 1000
};

/** @brief 清除结果独占发送机会，直到异步发送及总线保护间隔结束。 */
typedef enum {
    APP_CLEAR_REPLY_NONE,    /**< 当前没有待完成的清除应答。 */
    APP_CLEAR_REPLY_WAITING, /**< 等待提交原结果帧，通信恢复后继续重试。 */
    APP_CLEAR_REPLY_SENDING  /**< 已提交，等待底层确认发送及保护间隔结束。 */
} AppClearReplyState;

static AppFlameDetection s_detection; // 本地三路分钟次数与历史数据，不随通信重初始化而清零。
static DevModbusSlave *s_communication; // 独占原始帧收发模式的Device对象。
static DevModbusRawFrame s_received_frame; // RS485完整接收帧邮箱，仅主循环消费。
static bool s_app_initialized = false; // 应用上下文是否已建立，不表示通信或三路传感器均初始化成功。
static bool s_communication_ready = false; // 通信驱动是否仍初始化成功。
static bool s_change_pending = false; // 等待合并发送的事件或状态变化。
static bool s_have_sent = false; // 是否提交过首帧，用于启动时立即上报。
static bool s_have_status_sent = false; // 是否已经发送过检测状态帧，首次初始化帧不替代检测状态。
static bool s_have_init_sent = false; // 是否已经发送过初始化状态帧。
static bool s_init_report_pending = false; // 等待发送初始化结果变化或周期快照。
static bool s_init_waited_for_status = false; // 初始化帧已让出一次发送机会，下一次优先避免饥饿。
static AppFlameInitReport s_last_init_report; // 最近提交发送的初始化快照，仅比较业务字段。
static uint32_t s_last_send_ms; // 最近提交发送的时刻，控制周期及最小间隔。
static uint32_t s_last_status_send_ms; // 最近检测状态帧提交时刻，独立维护检测周期。
static uint32_t s_last_init_send_ms; // 最近初始化状态帧提交时刻，供晚接入的接收端周期读取。
static uint32_t s_last_monitor_ms; // 最近本地三路监视时刻。
static uint32_t s_last_comm_attempt_ms; // 最近通信初始化尝试，避免忙循环重试。
static uint16_t s_sequence; // 下一帧序号，仅提交成功时递增。
static uint8_t s_frame[APP_FLAME_MINUTE_FRAME_BYTES]; // 分钟及初始化共用编码缓冲区，最大80字节，Port提交前复制。
static AppClearReplyState s_clear_reply_state = APP_CLEAR_REPLY_NONE; // 独立清除结果的发送状态。
static AppFlameClearReport s_clear_report; // 本次执行结果快照，不被监视及初始化重试覆盖。
static uint8_t s_clear_frame[APP_FLAME_CLEAR_RESULT_FRAME_BYTES]; // 原应答固定字节，恢复重传时原样使用。
static bool s_clear_frame_ready = false; // 固定快照是否已经成功编码。
static bool s_clear_sequence_reserved = false; // 本应答是否已经占用一个全局发送序号，重传不重复递增。
volatile DevModbusSlaveStatus g_app_communication_status = DEV_MODBUS_SLAVE_NOT_INITIALIZED; // 最近通信结果。

/** @brief 比较初始化业务状态与最近提交的快照，不把时间或发送序号当作状态变化。
 * @param report 当前三路初始化快照。
 * @return 尚未发送初始化帧或状态、配置结果、次数及策略变化时返回true。 */
static bool app_main_init_report_changed(const AppFlameInitReport *report)
{
    uint8_t index; // 当前比较的初始化通道索引。
    if (!s_have_init_sent || (report->retry_limit != s_last_init_report.retry_limit) ||
        (report->retry_interval_ms != s_last_init_report.retry_interval_ms)) {
        return true;
    }
    for (index = 0; index < DEV_FLAME_CHANNEL_COUNT; ++index) {
        const AppFlameInitChannelReport *current = &report->channels[index]; // 本轮最新初始化字段。
        const AppFlameInitChannelReport *previous = &s_last_init_report.channels[index]; // 已提交的初始化字段。
        if ((current->status != previous->status) || (current->result != previous->result) ||
            (current->retries_done != previous->retries_done)) {
            return true;
        }
    }
    return false;
}

/** @brief 建立裸机应用状态，按启动配置初始化检测通道并初始化独占原始帧通信。
 * @details 仅在应用启动调用，建立首分钟并清零次数及历史；传感器配置结果分别保存在各路状态中。
 *          传感器配置及最多5次额外重试全部在返回前完成，之后才启动通信。
 * @param now_ms 当前毫秒时间。
 * @param timing 固件入口提供的有效取时与等待回调。
 * @return 仅返回通信初始化结果或计时参数错误，不代表三路传感器均配置成功。 */
DevModbusSlaveStatus App_Main_Init(uint32_t now_ms, const AppFlameInitTiming *timing)
{
    s_app_initialized = false;
    if ((timing == NULL) || (timing->get_ms == NULL) || (timing->wait_ms == NULL)) {
        g_app_communication_status = DEV_MODBUS_SLAVE_ERROR_ARGUMENT;
        return g_app_communication_status;
    }
    s_sequence = 0;
    s_have_sent = false;
    s_have_status_sent = false;
    s_have_init_sent = false;
    s_init_report_pending = true;
    s_init_waited_for_status = false;
    s_change_pending = false;
    s_clear_reply_state = APP_CLEAR_REPLY_NONE;
    s_clear_frame_ready = false;
    s_clear_sequence_reserved = false;
    AppFlameDetection_Init(&s_detection, g_app_flame_setup, now_ms, timing); // 返回前逐路完成首次配置及有限重试。
    s_communication = GetModbusSlave();
    g_app_communication_status = (s_communication == NULL) ? DEV_MODBUS_SLAVE_ERROR_ARGUMENT :
        s_communication->init(s_communication, APP_FLAME_DEVICE_ADDRESS, NULL, &s_received_frame);
    now_ms = timing->get_ms(); // 启动等待已消耗实际时间，正常调度不能沿用进入初始化时的旧时刻。
    s_last_send_ms = now_ms;
    s_last_status_send_ms = now_ms;
    s_last_init_send_ms = now_ms;
    s_last_monitor_ms = now_ms;
    s_last_comm_attempt_ms = now_ms;
    s_communication_ready = (g_app_communication_status == DEV_MODBUS_SLAVE_OK);
    s_app_initialized = true; // 应用上下文已建立，各设备的成功或失败由各自状态表示。
    return g_app_communication_status;
}

/** @brief 保留本分钟事件次数及历史数据的参数接入或故障恢复操作。
 * @details 只尝试一次并重置额外重试计数，失败直接结束，不安排轮询重试。
 * @param channel 有效设备通道。
 * @param config 用户确认的完整配置。
 * @param now_ms 当前毫秒时间。
 * @return 本次设备配置结果，最终结果通过状态帧上报。 */
DevFlameResult App_Main_ConfigureFlame(DevFlameChannel channel, const DevFlameConfig *config, uint32_t now_ms)
{
    DevFlameResult result; // 本次配置或初始化结果。
    if (!s_app_initialized) {
        return DEV_FLAME_NOT_READY;
    }
    result = AppFlameDetection_Configure(&s_detection, channel, config, now_ms);
    s_change_pending = true;
    s_init_report_pending = true; // 即使新一轮首次结果与旧轮相同，也明确发送新的初始化快照。
    return result;
}

/** @brief 持续统计分钟事件，清除结果优先且独占发送，完成后恢复分钟及初始化上报。
 * @param now_ms 当前毫秒时间，允许自然回绕。 */
void App_Main_Poll(uint32_t now_ms)
{
    DevModbusSlaveStatus status; // 通信轮询或提交的当前状态。
    AppFlameMinuteReport report = {0}; // 本次三路分钟次数及诊断快照，避免直接发送结构体。
    AppFlameInitReport init_report = {0}; // 最近配置结果的独立快照，不与当前采样结果混用。
    uint32_t since_send; // 自最近提交以来的回绕安全时间差。
    uint16_t frame_bytes; // 本次实际提交的帧长，分钟80字节或初始化47字节。
    bool send_initialization; // 本次发送初始化快照还是检测状态快照。
    if (!s_app_initialized) {
        return;
    }
    /*当前通信轮询时间大于等于APP_FLAME_MONITOR_INTERVAL_MS时，进行一次检测轮询*/
    if ((uint32_t)(now_ms - s_last_monitor_ms) >= APP_FLAME_MONITOR_INTERVAL_MS) {
        s_change_pending = AppFlameDetection_Poll(&s_detection, now_ms) || s_change_pending;
        s_last_monitor_ms = now_ms;
    }

    /*生成配置快照 用于随后的上报*/
    AppFlameDetection_InitSnapshot(&s_detection, now_ms, &init_report);

    s_change_pending = AppFlameDetection_MinuteSnapshot(&s_detection, now_ms, &report) || s_change_pending;
    if (app_main_init_report_changed(&init_report) ||
        ((uint32_t)(now_ms - s_last_init_send_ms) >= APP_REPORT_PERIOD_MS)) {
        s_init_report_pending = true;
    }
    if (!s_communication_ready) {
        if ((s_communication == NULL) ||
            ((uint32_t)(now_ms - s_last_comm_attempt_ms) < APP_COMMUNICATION_RETRY_MS)) {
            return;
        }
        s_last_comm_attempt_ms = now_ms;
        status = s_communication->init(s_communication, APP_FLAME_DEVICE_ADDRESS, NULL, &s_received_frame);
        g_app_communication_status = status;
        s_communication_ready = (status == DEV_MODBUS_SLAVE_OK);
        if (!s_communication_ready) {
            return;
        }
    }
    status = s_communication->poll(s_communication);
    g_app_communication_status = status;
    if (status == DEV_MODBUS_SLAVE_BUSY) {
        return; /* Port持有的发送快照直到TC完成之前保持不变。 */
    }
    if ((status != DEV_MODBUS_SLAVE_OK) && (status != DEV_MODBUS_SLAVE_IDLE)) {
        if (s_clear_reply_state == APP_CLEAR_REPLY_SENDING) {
            s_clear_reply_state = APP_CLEAR_REPLY_WAITING; // 在途应答出错，恢复后重传原帧而不再次清除。
        }
        s_communication_ready = false;
        s_last_comm_attempt_ms = now_ms;
        return;
    }
    if (s_clear_reply_state == APP_CLEAR_REPLY_SENDING) {
        s_clear_reply_state = APP_CLEAR_REPLY_NONE; // poll不再BUSY且无错误，TC发送及保护间隔均已结束。
    }
    if (s_received_frame.ready) {
        bool clear_command = AppFlameCommand_IsClear(s_received_frame.data, s_received_frame.length,
                                                     APP_FLAME_DEVICE_ADDRESS); // 完整协议校验结果。
        s_received_frame.ready = false; /* 释放邮箱，下一条完整帧才能写入。 */
        if (clear_command && (s_clear_reply_state == APP_CLEAR_REPLY_NONE)) {
            s_clear_reply_state = APP_CLEAR_REPLY_WAITING; // 先关闭普通上报路径，再执行清除及保存结果。
            s_clear_frame_ready = false;
            s_clear_sequence_reserved = false;
            s_clear_report.address = APP_FLAME_DEVICE_ADDRESS;
            s_clear_report.sequence = s_sequence;
            s_clear_report.request_sequence =
                (uint16_t)s_received_frame.data[APP_FLAME_SEQUENCE_OFFSET] |
                (uint16_t)((uint16_t)s_received_frame.data[APP_FLAME_SEQUENCE_OFFSET + APP_FLAME_WORD_BYTES - 1U]
                           << APP_FLAME_BYTE_BITS);
            AppFlameDetection_Clear(&s_detection, now_ms, &s_clear_report);
            s_change_pending = true; /* 即使部分通道清除失败也提前上报实际结果。 */
        }
        /* 单请求协议要求上位机等待应答；等待期间的额外命令不覆盖已经保存的结果。 */
    }
    if (s_clear_reply_state == APP_CLEAR_REPLY_WAITING) {
        if (!s_clear_frame_ready) {
            s_clear_frame_ready = AppFlameClearFrame_Encode(&s_clear_report, s_clear_frame, sizeof(s_clear_frame));
            if (!s_clear_frame_ready) {
                g_app_communication_status = DEV_MODBUS_SLAVE_ERROR_ARGUMENT;
                return; // 编码失败也不能放行普通上报。
            }
        }
        status = s_communication->sendRaw(s_communication, s_clear_frame, sizeof(s_clear_frame));
        g_app_communication_status = status;
        if (status == DEV_MODBUS_SLAVE_OK) {
            if (!s_clear_sequence_reserved) {
                ++s_sequence;
                s_clear_sequence_reserved = true;
            }
            s_last_send_ms = now_ms;
            s_have_sent = true;
            s_clear_reply_state = APP_CLEAR_REPLY_SENDING;
        } else if (status != DEV_MODBUS_SLAVE_BUSY) {
            s_communication_ready = false;
            s_last_comm_attempt_ms = now_ms;
        }
        return; // 结果等待、提交成功及提交失败均只允许发送清除结果，不受普通100ms间隔限制。
    }
    since_send = now_ms - s_last_send_ms;
    if (s_have_sent && (since_send < APP_REPORT_MIN_INTERVAL_MS)) {
        return;
    }
    send_initialization = s_init_report_pending && (s_init_waited_for_status || !s_change_pending);
    if (send_initialization) {
        init_report.address = APP_FLAME_DEVICE_ADDRESS;
        init_report.sequence = s_sequence;
        frame_bytes = APP_FLAME_INIT_FRAME_BYTES;
        if (!AppFlameInitFrame_Encode(&init_report, s_frame, sizeof(s_frame))) {
            g_app_communication_status = DEV_MODBUS_SLAVE_ERROR_ARGUMENT;
            return;
        }
    } else {
        if (!s_change_pending && s_have_status_sent &&
            ((uint32_t)(now_ms - s_last_status_send_ms) < APP_REPORT_PERIOD_MS)) {
            return;
        }
        report.type = s_change_pending ? APP_FLAME_TYPE_MINUTE_CHANGE : APP_FLAME_TYPE_MINUTE_PERIODIC;
        report.address = APP_FLAME_DEVICE_ADDRESS;
        report.sequence = s_sequence;
        frame_bytes = APP_FLAME_MINUTE_FRAME_BYTES;
        if (!AppFlameMinuteFrame_Encode(&report, s_frame, sizeof(s_frame))) {
            g_app_communication_status = DEV_MODBUS_SLAVE_ERROR_ARGUMENT;
            return;
        }
    }
    status = s_communication->sendRaw(s_communication, s_frame, frame_bytes);
    g_app_communication_status = status;
    if (status == DEV_MODBUS_SLAVE_OK) {
        ++s_sequence;
        s_last_send_ms = now_ms;
        s_have_sent = true;
        if (send_initialization) {
            s_last_init_report = init_report;
            s_have_init_sent = true;
            s_last_init_send_ms = now_ms;
            s_init_report_pending = false;
            s_init_waited_for_status = false;
        } else {
            s_have_status_sent = true;
            s_last_status_send_ms = now_ms;
            s_change_pending = false;
            s_init_waited_for_status = s_init_report_pending;
        }
    } else if (status != DEV_MODBUS_SLAVE_BUSY) {
        s_communication_ready = false;
        s_last_comm_attempt_ms = now_ms;
    }
}
