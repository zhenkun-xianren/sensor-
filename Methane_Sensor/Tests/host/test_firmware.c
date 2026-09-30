#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app_display.h"
#include "app_ir_counter.h"
#include "bsp_byte_ring.h"
#include "bsp_display_map.h"
#include "bsp_actuator.h"
#include "dev_actuator.h"
#include "dev_display.h"
#include "dev_gpio.h"
#include "dev_methane.h"
#include "dev_ir_remote.h"
#include "port_display.h"
#include "port_ir_remote.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "port_methane.h"
#include "app_modbus.h"
#include "dev_modbus.h"
#include "bsp_rs485.h"
#include "port_modbus.h"

static uint8_t mock_frame[4];
static BspByteRing mock_uart_rx;
static uint8_t mock_uart_tx[7];
static int mock_gpio_init_result;
static uint8_t mock_optocoupler_state;
static uint8_t mock_relay_state;
static uint8_t mock_buzzer_state;
static uint8_t mock_actuator_initialized;
static uint8_t mock_rs485_tx[PORT_MODBUS_REQUEST_LENGTH];
static uint8_t mock_rs485_rx[PORT_MODBUS_RESPONSE_LENGTH];
static uint8_t mock_rs485_rx_length;
static uint8_t mock_rs485_rx_index;
static uint8_t mock_rs485_reply_mode;
static TickType_t mock_modbus_timer_period;
static BaseType_t mock_modbus_timer_auto_reload;
static TimerCallbackFunction_t mock_modbus_timer_callback;
static TaskHandle_t mock_notified_task;
static uint32_t mock_notification_count;

enum {
    MOCK_RS485_REPLY_ECHO = 0U,
    MOCK_RS485_REPLY_BAD_CRC,
    MOCK_RS485_REPLY_MISMATCH,
    MOCK_RS485_REPLY_TIMEOUT,
    MOCK_RS485_REPLY_EXCEPTION
};

/**
 * @brief 测试桩返回调用方提供的静态任务控制块。
 * @param task_code 被测试模块提供的任务入口。
 * @param name 被测试任务名称。
 * @param stack_depth 任务栈深度。
 * @param parameters 传递给任务入口的参数。
 * @param priority 任务优先级。
 * @param stack_buffer 调用方提供的任务栈。
 * @param task_buffer 调用方提供的静态任务控制块。
 * @return 模拟创建成功时返回静态任务句柄。
 */
TaskHandle_t xTaskCreateStatic(TaskFunction_t task_code, const char * const name,
                               const configSTACK_DEPTH_TYPE stack_depth,
                               void * const parameters, UBaseType_t priority,
                               StackType_t * const stack_buffer,
                               StaticTask_t * const task_buffer)
{
    (void)task_code;
    (void)name;
    (void)stack_depth;
    (void)parameters;
    (void)priority;
    (void)stack_buffer;
    return (TaskHandle_t)task_buffer;
}

/**
 * @brief 测试桩忽略任务延时，避免主机测试依赖调度器。
 * @param ticks_to_delay 请求延时的系统节拍数。
 */
void vTaskDelay(const TickType_t ticks_to_delay) { (void)ticks_to_delay; }

/**
 * @brief 测试桩返回固定的系统节拍计数。
 * @return 主机测试中的系统节拍值 0。
 */
TickType_t xTaskGetTickCount(void) { return 0U; }

/**
 * @brief 测试桩返回调用方提供的静态软件定时器缓冲区。
 * @param name 被测试软件定时器名称。
 * @param period 定时器周期，单位为系统节拍。
 * @param auto_reload 非零表示自动重载。
 * @param timer_id 分配给定时器的用户标识。
 * @param callback 定时器到期回调函数。
 * @param timer_buffer 调用方提供的静态定时器缓冲区。
 * @return 模拟创建成功时返回静态定时器句柄。
 */
TimerHandle_t xTimerCreateStatic(const char * const name,
                                 const TickType_t period,
                                 const BaseType_t auto_reload,
                                 void * const timer_id,
                                 TimerCallbackFunction_t callback,
                                 StaticTimer_t *timer_buffer)
{
    if (strcmp(name, "mb_tx") == 0) {
        mock_modbus_timer_period = period;
        mock_modbus_timer_auto_reload = auto_reload;
        mock_modbus_timer_callback = callback;
    }
    (void)timer_id;
    return (TimerHandle_t)timer_buffer;
}

/**
 * @brief 测试桩记录任务通知，用于确认定时器回调唤醒 Modbus 任务。
 * @param task 接收通知的任务句柄。
 * @param index 通知数组索引。
 * @param value 通知附带的数值。
 * @param action 通知值更新方式。
 * @param previousValue 接收通知前数值的可选输出指针。
 * @return 模拟通知成功时返回 pdPASS。
 */
BaseType_t xTaskGenericNotify(TaskHandle_t task, UBaseType_t index,
                              uint32_t value, eNotifyAction action,
                              uint32_t *previousValue)
{
    (void)index;
    (void)value;
    (void)action;
    (void)previousValue;
    mock_notified_task = task;
    ++mock_notification_count;
    return pdPASS;
}

/**
 * @brief 测试桩模拟任务阻塞等待通知并返回无待处理通知。
 * @param index 等待的通知数组索引。
 * @param clearOnExit 非零时表示取出通知后清零计数。
 * @param ticksToWait 最长等待节拍数。
 * @return 主机测试中固定返回零，表示当前没有待处理通知。
 */
uint32_t ulTaskGenericNotifyTake(UBaseType_t index, BaseType_t clearOnExit,
                                 TickType_t ticksToWait)
{
    (void)index;
    (void)clearOnExit;
    (void)ticksToWait;
    return 0U;
}

/**
 * @brief 测试桩报告软件定时器命令执行成功。
 * @param timer 接收命令的软件定时器句柄。
 * @param command_id 要执行的软件定时器命令。
 * @param optional_value 命令附带的可选节拍值。
 * @param higher_priority_task_woken 接收高优先级任务唤醒标记的地址。
 * @param ticks_to_wait 命令队列满时允许阻塞等待的节拍数。
 * @return 模拟命令执行成功时返回 pdPASS。
 */
BaseType_t xTimerGenericCommandFromTask(TimerHandle_t timer,
                                        const BaseType_t command_id,
                                        const TickType_t optional_value,
                                        BaseType_t * const higher_priority_task_woken,
                                        const TickType_t ticks_to_wait)
{
    (void)timer;
    (void)command_id;
    (void)optional_value;
    (void)higher_priority_task_woken;
    (void)ticks_to_wait;
    return pdPASS;
}

int BSP_Gpio_Init(void) { return mock_gpio_init_result; }
void BSP_Display_Init(void) { }
void BSP_Display_ScanStep(void) { }
void BSP_Display_SetFrame(const uint8_t frame[4]) { memcpy(mock_frame, frame, 4U); }

/**
 * @brief 测试桩记录执行器初始化调用。
 */
void BSP_Actuator_Init(void) { mock_actuator_initialized = 1U; }

/**
 * @brief 测试桩记录光耦隔离输出状态。
 * @param enabled 测试光耦输出状态。
 */
void BSP_Actuator_SetOptocoupler(uint8_t enabled) { mock_optocoupler_state = enabled; }

/**
 * @brief 测试桩记录继电器输出状态。
 * @param enabled 测试继电器状态。
 */
void BSP_Actuator_SetRelay(uint8_t enabled) { mock_relay_state = enabled; }

/**
 * @brief 测试桩记录蜂鸣器输出状态。
 * @param enabled 测试蜂鸣器状态。
 */
void BSP_Actuator_SetBuzzer(uint8_t enabled) { mock_buzzer_state = enabled; }

void BSP_Display_SetDigit(uint8_t index, uint8_t glyph)
{
    if (index < 4U) mock_frame[index] = glyph;
}
void BSP_Ir_Init(void) { }
void BSP_Uart3_Init(void) { BSP_ByteRing_Init(&mock_uart_rx); }
uint8_t BSP_Uart3_Write(const uint8_t *data, uint8_t length)
{
    if (length != 7U) return 0U;
    memcpy(mock_uart_tx, data, length);
    return 1U;
}
uint8_t BSP_Uart3_Read(uint8_t *data) { return BSP_ByteRing_Pop(&mock_uart_rx, data); }
uint8_t BSP_Uart3_TakeOverrun(void) { return 0U; }

/**
 * @brief 计算主机测试桩应答帧的 Modbus CRC-16。
 * @param data 待校验的数据缓冲区。
 * @param length 参与校验的字节数。
 * @return 计算得到的 16 位 CRC 值。
 */
static uint16_t mock_modbus_crc16(const uint8_t *data, uint8_t length)
{
    uint16_t crc = 0xFFFFU;
    uint8_t byteIndex;
    for (byteIndex = 0U; byteIndex < length; ++byteIndex) {
        uint8_t bitIndex;
        crc ^= data[byteIndex];
        for (bitIndex = 0U; bitIndex < 8U; ++bitIndex) {
            if ((crc & 1U) != 0U) {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            } else {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

/**
 * @brief 按当前测试模式为 Modbus 主站准备模拟从站应答。
 * @param request 主站刚发送的 0x06 请求帧。
 * @param length 请求帧长度。
 */
static void mock_rs485_prepare_reply(const uint8_t *request, uint8_t length)
{
    uint16_t crc;
    mock_rs485_rx_index = 0U;
    mock_rs485_rx_length = 0U;
    if (length != PORT_MODBUS_REQUEST_LENGTH ||
        mock_rs485_reply_mode == MOCK_RS485_REPLY_TIMEOUT) {
        return;
    }

    if (mock_rs485_reply_mode == MOCK_RS485_REPLY_EXCEPTION) {
        mock_rs485_rx[0] = request[0];
        mock_rs485_rx[1] = (uint8_t)(PORT_MODBUS_WRITE_SINGLE_REGISTER |
                                     PORT_MODBUS_EXCEPTION_FUNCTION_MASK);
        mock_rs485_rx[2] = 2U;
        crc = mock_modbus_crc16(mock_rs485_rx, 3U);
        mock_rs485_rx[3] = (uint8_t)crc;
        mock_rs485_rx[4] = (uint8_t)(crc >> 8U);
        mock_rs485_rx_length = PORT_MODBUS_EXCEPTION_LENGTH;
        return;
    }

    memcpy(mock_rs485_rx, request, PORT_MODBUS_RESPONSE_LENGTH);
    mock_rs485_rx_length = PORT_MODBUS_RESPONSE_LENGTH;
    if (mock_rs485_reply_mode == MOCK_RS485_REPLY_BAD_CRC) {
        mock_rs485_rx[PORT_MODBUS_RESPONSE_LENGTH - 1U] ^= 1U;
    } else if (mock_rs485_reply_mode == MOCK_RS485_REPLY_MISMATCH) {
        mock_rs485_rx[5] ^= 1U;
        crc = mock_modbus_crc16(mock_rs485_rx,
                                (uint8_t)(PORT_MODBUS_RESPONSE_LENGTH - 2U));
        mock_rs485_rx[PORT_MODBUS_RESPONSE_LENGTH - 2U] = (uint8_t)crc;
        mock_rs485_rx[PORT_MODBUS_RESPONSE_LENGTH - 1U] = (uint8_t)(crc >> 8U);
    }
}

/**
 * @brief 测试桩模拟 RS485 板级初始化。
 * @return 初始化成功返回 BSP_RS485_OK。
 */
BspRs485Status BSP_Rs485_Init(void) { return BSP_RS485_OK; }

/**
 * @brief 测试桩保存 RS485 发送帧并生成配置的模拟应答。
 * @param data 待发送的字节序列。
 * @param length 字节序列长度。
 * @return 数据有效时返回 BSP_RS485_OK，否则返回 BSP_RS485_INVALID。
 */
BspRs485Status BSP_Rs485_Send(const uint8_t *data, uint8_t length)
{
    if (data == NULL || length != PORT_MODBUS_REQUEST_LENGTH) {
        return BSP_RS485_INVALID;
    }
    memcpy(mock_rs485_tx, data, length);
    mock_rs485_prepare_reply(data, length);
    return BSP_RS485_OK;
}

/**
 * @brief 测试桩清空模拟 RS485 接收应答。
 */
void BSP_Rs485_ClearRx(void)
{
    mock_rs485_rx_index = 0U;
    mock_rs485_rx_length = 0U;
}

/**
 * @brief 测试桩从模拟 RS485 应答缓冲区取出一个字节。
 * @param data 用于保存接收字节的输出指针。
 * @param timeoutMs 本次等待允许的最长时间，主机桩不使用该参数。
 * @return 有应答字节时返回 BSP_RS485_OK；没有应答时返回 BSP_RS485_TIMEOUT。
 */
BspRs485Status BSP_Rs485_ReadByte(uint8_t *data, uint32_t timeoutMs)
{
    (void)timeoutMs;
    if (data == NULL) {
        return BSP_RS485_INVALID;
    }
    if (mock_rs485_rx_index >= mock_rs485_rx_length) {
        return BSP_RS485_TIMEOUT;
    }
    *data = mock_rs485_rx[mock_rs485_rx_index++];
    return BSP_RS485_OK;
}

/**
 * @brief 测试桩返回固定的毫秒时基。
 * @return 主机测试中的系统毫秒值 0。
 */
uint32_t BSP_Rs485_GetTickMs(void) { return 0U; }

/**
 * @brief 测试桩忽略真实 USART2 接收中断。
 */
void BSP_Rs485_IrqHandler(void) { }

/**
 * @brief 验证显示编码和 Device 到 Port 的函数指针配对。
 */
static void test_display(void)
{
    static const uint8_t glyphs[10] = {
        0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U,
        0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU
    };
    for (uint8_t i = 0U; i < 10U; ++i) {
        assert(PortDisplay_Glyph(i) == glyphs[i]);
    }
    for (uint8_t bit = 0U; bit < 7U; ++bit) {
        assert(BSP_Display_SegmentPins((uint8_t)(1U << bit)) == (uint16_t)(1U << (9U - bit)));
    }
    assert(BSP_Display_DigitPin(0U) == (1U << 11));
    assert(BSP_Display_DigitPin(1U) == (1U << 10));
    assert(BSP_Display_DigitPin(2U) == (1U << 9));
    assert(BSP_Display_DigitPin(3U) == (1U << 12));
    uint8_t frame[4];
    DevDisplay *display = GetDisplay();
    assert(display != NULL && display->init != NULL && display->scanStep != NULL);
    assert(display->clear != NULL && display->showDigit != NULL);
    assert(display->showInteger4 != NULL && display->showFixedPoint != NULL);
    assert(display->showError != NULL && display->showAll != NULL);
    PortDisplay_EncodeFixedPoint(0U, 0U, frame);
    assert(frame[0] == 0U && frame[1] == 0U && frame[2] == 0U);
    assert(frame[3] == glyphs[0]);
    PortDisplay_EncodeFixedPoint(123U, 0U, frame);
    assert(frame[0] == 0U && frame[1] == glyphs[1] && frame[2] == glyphs[2]);
    assert(frame[3] == glyphs[3]);
    PortDisplay_EncodeFixedPoint(0U, 1U, frame);
    assert(frame[0] == 0U && frame[1] == 0U);
    assert(frame[2] == (uint8_t)(glyphs[0] | 0x80U) && frame[3] == glyphs[0]);
    PortDisplay_EncodeFixedPoint(5U, 1U, frame);
    assert(frame[0] == 0U && frame[1] == 0U);
    assert(frame[2] == (uint8_t)(glyphs[0] | 0x80U) && frame[3] == glyphs[5]);
    PortDisplay_EncodeFixedPoint(1234U, 1U, frame);
    assert(frame[0] == glyphs[1] && frame[1] == glyphs[2]);
    assert(frame[2] == (uint8_t)(glyphs[3] | 0x80U) && frame[3] == glyphs[4]);
    PortDisplay_EncodeFixedPoint(5U, 2U, frame);
    assert(frame[0] == 0U && frame[1] == (uint8_t)(glyphs[0] | 0x80U));
    assert(frame[2] == glyphs[0] && frame[3] == glyphs[5]);
    PortDisplay_EncodeFixedPoint(1234U, 2U, frame);
    assert(frame[0] == glyphs[1] && frame[1] == (uint8_t)(glyphs[2] | 0x80U));
    assert(frame[2] == glyphs[3] && frame[3] == glyphs[4]);
    PortDisplay_EncodeFixedPoint(9999U, 2U, frame);
    assert(frame[0] == glyphs[9] && frame[1] == (uint8_t)(glyphs[9] | 0x80U));
    assert(frame[2] == glyphs[9] && frame[3] == glyphs[9]);
    PortDisplay_EncodeFixedPoint(9999U, 3U, frame);
    assert(frame[0] == (uint8_t)(glyphs[9] | 0x80U) && frame[1] == glyphs[9]);
    assert(frame[2] == glyphs[9] && frame[3] == glyphs[9]);
    PortDisplay_EncodeFixedPoint(10000U, 2U, frame);
    assert(frame[0] == 0U && frame[1] == 0x79U);
    assert(frame[2] == glyphs[0] && frame[3] == glyphs[4]);
    PortDisplay_EncodeFixedPoint(123U, 4U, frame);
    assert(frame[0] == 0U && frame[1] == 0x79U);
    assert(frame[2] == glyphs[0] && frame[3] == glyphs[4]);
    PortDisplay_EncodeError(3U, frame);
    assert(frame[0] == 0U && frame[1] == 0x79U);
    assert(frame[2] == glyphs[0] && frame[3] == glyphs[3]);
    PortDisplay_EncodeInteger4(0U, frame);
    assert(frame[0] == glyphs[0] && frame[1] == glyphs[0]);
    assert(frame[2] == glyphs[0] && frame[3] == glyphs[0]);
    PortDisplay_EncodeInteger4(1U, frame);
    assert(frame[0] == glyphs[0] && frame[1] == glyphs[0]);
    assert(frame[2] == glyphs[0] && frame[3] == glyphs[1]);
    PortDisplay_EncodeInteger4(9999U, frame);
    assert(frame[0] == glyphs[9] && frame[1] == glyphs[9]);
    assert(frame[2] == glyphs[9] && frame[3] == glyphs[9]);
    PortDisplay_EncodeInteger4(10000U, frame);
    assert(frame[0] == 0U && frame[1] == 0x79U);
    assert(frame[2] == glyphs[0] && frame[3] == glyphs[4]);
    mock_frame[0] = glyphs[1];
    mock_frame[1] = glyphs[2];
    mock_frame[2] = glyphs[3];
    mock_frame[3] = glyphs[4];
    display->showDigit(display, DEV_DISPLAY_DIG3, 7U,
                       DEV_DISPLAY_DECIMAL_POINT_ON);
    assert(mock_frame[0] == glyphs[1]);
    assert(mock_frame[1] == glyphs[2]);
    assert(mock_frame[2] == (uint8_t)(glyphs[7] | 0x80U));
    assert(mock_frame[3] == glyphs[4]);

    display->showDigit(display, DEV_DISPLAY_DIG1, 8U,
                       DEV_DISPLAY_DECIMAL_POINT_OFF);
    display->showDigit(display, DEV_DISPLAY_DIG4, 9U,
                       DEV_DISPLAY_DECIMAL_POINT_ON);
    assert(mock_frame[0] == glyphs[8]);
    assert(mock_frame[3] == (uint8_t)(glyphs[9] | 0x80U));

    {
        uint8_t previous_frame[4];
        memcpy(previous_frame, mock_frame, sizeof(previous_frame));
        display->showDigit(display, (DevDisplayPosition)4U, 0U,
                           DEV_DISPLAY_DECIMAL_POINT_ON);
        assert(memcmp(mock_frame, previous_frame, sizeof(mock_frame)) == 0);
    }
}

/**
 * @brief 验证执行器 Device 方法绑定及状态传递。
 */
static void test_actuator(void)
{
    DevActuator *actuator = GetActuator();
    assert(actuator != NULL && actuator->init != NULL);
    assert(actuator->setRelay != NULL && actuator->setOptocoupler != NULL &&
           actuator->setBuzzer != NULL);
    mock_actuator_initialized = 0U;
    actuator->init(actuator);
    assert(mock_actuator_initialized == 1U);
    actuator->setRelay(actuator, 1U);
    actuator->setOptocoupler(actuator, 1U);
    actuator->setBuzzer(actuator, 1U);
    assert(mock_relay_state == 1U && mock_optocoupler_state == 1U &&
           mock_buzzer_state == 1U);
    actuator->setRelay(actuator, 0U);
    actuator->setOptocoupler(actuator, 0U);
    actuator->setBuzzer(actuator, 0U);
    assert(mock_relay_state == 0U && mock_optocoupler_state == 0U &&
           mock_buzzer_state == 0U);
}

static void test_gpio_port(void)
{
    DevGpio *gpio = GetGpio();
    assert(gpio != NULL && gpio->init != NULL);
    mock_gpio_init_result = 0;
    assert(gpio->init(gpio) == DEV_OK);
    mock_gpio_init_result = -1;
    assert(gpio->init(gpio) == DEV_IO_ERROR);
    assert(gpio->init(NULL) == DEV_IO_ERROR);
}

static void test_methane_commands(void)
{
    uint8_t frame[7];
    assert(PortMethane_BuildCommand(METHANE_CMD_R0, frame) == 7U);
    assert(memcmp(frame, "R0\t7E\r\n", 7U) == 0);
    assert(PortMethane_BuildCommand(METHANE_CMD_R8, frame) == 7U);
    assert(memcmp(frame, "R8\t76\r\n", 7U) == 0);
    assert(PortMethane_BuildCommand(METHANE_CMD_F0, frame) == 7U);
    assert(memcmp(frame, "F0\t8A\r\n", 7U) == 0);
    assert(PortMethane_BuildCommand(METHANE_CMD_S2, frame) == 7U);
    assert(memcmp(frame, "S2\t7B\r\n", 7U) == 0);
    assert(PortMethane_BuildCommand((MethaneSafeCommand)12, frame) == 0U);
}

/**
 * @brief 验证甲烷响应解析和 Device 到 Port 的请求/轮询调用。
 */
static void test_methane_response(void)
{
    static const uint8_t valid[] = "+002.00,+25.0,1013.25,00\t87\r\n";
    static const uint8_t status[] = "+002.00,+25.0,1013.25,02\t85\r\n";
    uint8_t damaged[sizeof(valid)];
    MethaneFrameBuffer buffer;
    MethaneSample sample;
    DevMethane *methane = GetMethane();
    DevStatus result = DEV_PENDING;
    assert(methane != NULL && methane->init != NULL);
    assert(methane->sendSafeCommand != NULL && methane->requestSample != NULL);
    assert(methane->pollSample != NULL && methane->resetResponse != NULL);
    assert(PortMethane_Lrc(valid, 24U) == 0x87U);
    assert(PortMethane_ParseR8(valid, (uint8_t)(sizeof(valid) - 1U), &sample) == DEV_OK);
    assert(sample.concentration_centi_vol == 200U);
    assert(sample.temperature_deci_c == 250);
    assert(sample.pressure_centi_mbar == 101325U);
    assert(sample.status_code == 0U);
    assert(PortMethane_ParseR8(status, (uint8_t)(sizeof(status) - 1U), &sample) == DEV_SENSOR_ERROR);
    assert(sample.status_code == 2U);
    memcpy(damaged, valid, sizeof(valid));
    damaged[10] = '6';
    assert(PortMethane_ParseR8(damaged, (uint8_t)(sizeof(valid) - 1U), &sample) == DEV_LRC_ERROR);
    assert(PortMethane_ParseR8(valid, 20U, &sample) == DEV_FORMAT_ERROR);
    PortMethane_BufferReset(&buffer);
    for (uint8_t i = 0U; i < sizeof(valid) - 1U; ++i) {
        result = PortMethane_BufferPush(&buffer, valid[i], &sample);
    }
    assert(result == DEV_OK && buffer.length == 0U);
    methane->init(methane);
    assert(methane->requestSample(methane) == DEV_OK);
    assert(memcmp(mock_uart_tx, "R8\t76\r\n", 7U) == 0);
    for (uint8_t i = 0U; i < sizeof(valid) - 1U; ++i) {
        assert(BSP_ByteRing_Push(&mock_uart_rx, valid[i]) != 0U);
    }
    assert(methane->pollSample(methane, &sample) == DEV_OK);
    assert(sample.concentration_centi_vol == 200U);
}

/**
 * @brief 验证 Modbus RTU 0x06 帧、回显、异常帧及错误响应处理。
 */
static void test_modbus_rtu(void)
{
    static const uint8_t expected_request[PORT_MODBUS_REQUEST_LENGTH] = {
        0x01U, 0x06U, 0x00U, 0x00U, 0x00U, 0x00U, 0x89U, 0xCAU
    };
    DevModbus *modbus = GetModbus();

    assert(modbus != NULL && modbus->init != NULL &&
           modbus->writeHoldingRegister != NULL);
    assert(modbus->init(modbus) == DEV_MODBUS_OK);

    mock_rs485_reply_mode = MOCK_RS485_REPLY_ECHO;
    assert(modbus->writeHoldingRegister(modbus, 1U, 0x0000U, 0x0000U) == DEV_MODBUS_OK);
    assert(memcmp(mock_rs485_tx, expected_request, sizeof(expected_request)) == 0);

    mock_rs485_reply_mode = MOCK_RS485_REPLY_BAD_CRC;
    assert(modbus->writeHoldingRegister(modbus, 1U, 0x0000U, 0x0001U) ==
           DEV_MODBUS_ERROR_CRC);

    mock_rs485_reply_mode = MOCK_RS485_REPLY_MISMATCH;
    assert(modbus->writeHoldingRegister(modbus, 1U, 0x0000U, 0x0002U) ==
           DEV_MODBUS_ERROR_RESPONSE);

    mock_rs485_reply_mode = MOCK_RS485_REPLY_EXCEPTION;
    assert(modbus->writeHoldingRegister(modbus, 1U, 0x0000U, 0x0003U) ==
           DEV_MODBUS_ERROR_EXCEPTION);

    mock_rs485_reply_mode = MOCK_RS485_REPLY_TIMEOUT;
    assert(modbus->writeHoldingRegister(modbus, 1U, 0x0000U, 0x0004U) ==
           DEV_MODBUS_ERROR_TIMEOUT);
    assert(modbus->writeHoldingRegister(modbus, 0U, 0x0000U, 0x0000U) ==
           DEV_MODBUS_ERROR_ARGUMENT);
}

/**
 * @brief 验证 Modbus APP 使用每秒自动重载的软件定时器通知静态任务。
 */
static void test_modbus_app_period(void)
{
    uint32_t notificationCount = mock_notification_count;

    assert(AppModbus_Init() == 1U);
    assert(mock_modbus_timer_period == pdMS_TO_TICKS(1000U));
    assert(mock_modbus_timer_auto_reload == pdTRUE);
    assert(mock_modbus_timer_callback != NULL);
    assert(AppModbus_GetLastStatus() == DEV_MODBUS_OK);
    mock_modbus_timer_callback((TimerHandle_t)1U);
    assert(mock_notification_count == notificationCount + 1U);
    assert(mock_notified_task != NULL);
}

static void test_ring_wrap(void)
{
    BspByteRing ring;
    uint8_t byte;
    BSP_ByteRing_Init(&ring);
    for (uint16_t i = 0U; i < 120U; ++i) assert(BSP_ByteRing_Push(&ring, (uint8_t)i));
    for (uint16_t i = 0U; i < 100U; ++i) {
        assert(BSP_ByteRing_Pop(&ring, &byte));
        assert(byte == (uint8_t)i);
    }
    for (uint16_t i = 120U; i < 220U; ++i) assert(BSP_ByteRing_Push(&ring, (uint8_t)i));
    for (uint16_t i = 100U; i < 220U; ++i) {
        assert(BSP_ByteRing_Pop(&ring, &byte));
        assert(byte == (uint8_t)i);
    }
    assert(!BSP_ByteRing_Pop(&ring, &byte));
    for (uint16_t i = 0U; i < 127U; ++i) assert(BSP_ByteRing_Push(&ring, (uint8_t)i));
    assert(!BSP_ByteRing_Push(&ring, 0U));
}

static uint8_t feed_nec(NecDecoder *decoder, uint32_t raw, uint16_t mark,
                        uint16_t leader_space, uint16_t zero_space,
                        uint16_t one_space, IrKeyEvent *event)
{
    uint8_t result = 0U;
    result |= PortIr_FeedPulse(decoder, 0U, mark, event);
    result |= PortIr_FeedPulse(decoder, 1U, leader_space, event);
    for (uint8_t bit = 0U; bit < 32U; ++bit) {
        result |= PortIr_FeedPulse(decoder, 0U, 560U, event);
        result |= PortIr_FeedPulse(decoder, 1U,
                 (raw & ((uint32_t)1U << bit)) != 0U ? one_space : zero_space, event);
    }
    return result;
}

static void test_nec(void)
{
    NecDecoder decoder;
    IrKeyEvent event;
    uint32_t standard = 0xF30CFF00UL;
    uint32_t extended = 0xA55A1234UL;
    PortIr_DecoderInit(&decoder);
    assert(feed_nec(&decoder, standard, 9000U, 4500U, 560U, 1690U, &event));
    assert(event.address == 0U && event.command == 0x0CU && event.digit == 1);
    assert(event.repeat == 0U && event.raw_code == standard);
    assert(!PortIr_FeedPulse(&decoder, 0U, 9000U, &event));
    assert(!PortIr_FeedPulse(&decoder, 1U, 2250U, &event));
    assert(PortIr_FeedPulse(&decoder, 0U, 560U, &event));
    assert(event.repeat == 1U && event.digit == 1);
    assert(feed_nec(&decoder, extended, 8000U, 3500U, 900U, 1300U, &event));
    assert(event.address == 0x1234U && event.command == 0x5AU && event.digit == 6);
    assert(!feed_nec(&decoder, 0x000CFF00UL, 9000U, 4500U, 560U, 1690U, &event));
    assert(!PortIr_FeedPulse(&decoder, 0U, 9000U, &event));
    assert(!PortIr_FeedPulse(&decoder, 1U, 3000U, &event));
    assert(!PortIr_FeedPulse(&decoder, 0U, 560U, &event));
    assert(!PortIr_FeedPulse(&decoder, 1U, 20000U, &event));
    assert(feed_nec(&decoder, standard, 9000U, 4500U, 560U, 1690U, &event));
    for (uint8_t digit = 0U; digit < 10U; ++digit) {
        static const uint8_t codes[10] = {0x16U,0x0CU,0x18U,0x5EU,0x08U,
                                           0x1CU,0x5AU,0x42U,0x52U,0x4AU};
        assert(PortIr_CommandDigit(codes[digit]) == (int8_t)digit);
    }
    assert(PortIr_CommandDigit(0xFFU) == -1);
}

/**
 * @brief 验证红外数字事件提取、重复帧忽略和非法数字拒绝。
 */
static void test_ir_counter(void)
{
    IrKeyEvent event = {0U, 0xFFU, 1, 0U, 0x12345678UL};
    uint8_t digit = 0xFFU;

    assert(AppIrCounter_GetDisplayDigit(&event, &digit) == 1U);
    assert(digit == 1U);
    event.repeat = 1U;
    assert(AppIrCounter_GetDisplayDigit(&event, &digit) == 0U);
    assert(digit == 1U);
    event.repeat = 0U;
    event.digit = 0;
    assert(AppIrCounter_GetDisplayDigit(&event, &digit) == 1U);
    assert(digit == 0U);
    event.digit = 9;
    assert(AppIrCounter_GetDisplayDigit(&event, &digit) == 1U);
    assert(digit == 9U);
    event.digit = -1;
    assert(AppIrCounter_GetDisplayDigit(&event, &digit) == 0U);
    event.digit = 10;
    assert(AppIrCounter_GetDisplayDigit(&event, &digit) == 0U);
    assert(AppIrCounter_GetDisplayDigit(NULL, &digit) == 0U);
    assert(AppIrCounter_GetDisplayDigit(&event, NULL) == 0U);
}

/**
 * @brief 验证红外计数模块启动时将显示初始化为四位零。
 */
static void test_ir_counter_startup(void)
{
    static const uint8_t zero_glyph = 0x3FU;

    assert(AppIrCounter_Init() == 1U);
    assert(mock_frame[0] == zero_glyph && mock_frame[1] == zero_glyph);
    assert(mock_frame[2] == zero_glyph && mock_frame[3] == zero_glyph);
}

static void test_display_priority(void)
{
    assert(AppDisplay_Choose(1U, 1U, 1U, 1U) == APP_DISPLAY_INIT_FAULT);
    assert(AppDisplay_Choose(0U, 1U, 1U, 1U) == APP_DISPLAY_DIAGNOSTIC);
    assert(AppDisplay_Choose(0U, 0U, 1U, 1U) == APP_DISPLAY_REMOTE);
    assert(AppDisplay_Choose(0U, 0U, 0U, 1U) == APP_DISPLAY_SENSOR_ERROR);
    assert(AppDisplay_Choose(0U, 0U, 0U, 0U) == APP_DISPLAY_CONCENTRATION);
}

int main(void)
{
    test_gpio_port();
    test_display();
    test_actuator();
    test_methane_commands();
    test_methane_response();
    test_modbus_rtu();
    test_modbus_app_period();
    test_ring_wrap();
    test_nec();
    test_ir_counter();
    test_ir_counter_startup();
    test_display_priority();
    puts("host firmware tests passed");
    return 0;
}
