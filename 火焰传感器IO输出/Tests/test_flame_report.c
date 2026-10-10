#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_main.h"
#include "bsp_rs485.h"

static DevFlame devices[3];
static AppFlameDetection *observed_detection;
static bool pending_flags[3];
static DevFlameResult read_results[3], pending_results[3], init_results[3];
static DevFlameSample samples[3];
static unsigned reads, inits;
static uint32_t fake_now_ms;

static uint32_t fake_get_ms(void) { return fake_now_ms; }
static bool fake_wait_ms(uint32_t interval_ms) { fake_now_ms += interval_ms; return true; }
static const AppFlameInitTiming fake_init_timing = { fake_get_ms, fake_wait_ms };
static bool require_latch;
static BspRs485Status transport_poll = BSP_RS485_OK, transport_send = BSP_RS485_OK;
static const uint8_t *tx_pointer;
static uint16_t tx_length;
static unsigned tx_count;
static uint8_t rx[256];
static uint16_t rx_length;
static bool rx_ready;

static unsigned channel_of(DevFlame *self) { return (unsigned)(self - devices); }
static DevFlameResult fake_init(DevFlame *self, const DevFlameConfig *config)
{
    assert(config->detection_enabled && config->interrupt_source == DEV_FLAME_INTERRUPT_EVENT);
    ++inits;
    return init_results[channel_of(self)];
}
static DevFlameResult fake_pending(DevFlame *self, bool *pending)
{
    unsigned c = channel_of(self);
    if (pending_results[c] == DEV_FLAME_OK) { *pending = pending_flags[c]; }
    return pending_results[c];
}
static DevFlameResult fake_read(DevFlame *self, DevFlameReadMode mode, DevFlameSample *sample)
{
    unsigned c = channel_of(self);
    assert(mode == DEV_FLAME_READ_READY); /* 强制读取不能成为事件依据。 */
    if (require_latch) { assert(observed_detection->channels[c].triggered); }
    ++reads;
    if (read_results[c] == DEV_FLAME_OK) {
        *sample = samples[c];
        pending_flags[c] = false; /* 模拟读取清除探头事件。 */
    }
    return read_results[c];
}
DevFlame *GetFlame(DevFlameChannel channel)
{
    if (channel < DEV_FLAME_U22 || channel >= DEV_FLAME_CHANNEL_COUNT) { return NULL; }
    devices[channel].init = fake_init;
    devices[channel].pending = fake_pending;
    devices[channel].read = fake_read;
    return &devices[channel];
}
BspRs485Status BSP_Rs485_Init(void) { transport_poll = BSP_RS485_OK; rx_ready = false; return BSP_RS485_OK; }
BspRs485Status BSP_Rs485_Poll(void) { return transport_poll; }
BspRs485Status BSP_Rs485_AcquireFrame(const uint8_t **data, uint16_t *length)
{
    if (!rx_ready) { return BSP_RS485_NO_FRAME; }
    *data = rx; *length = rx_length; return BSP_RS485_OK;
}
void BSP_Rs485_ReleaseFrame(void) { rx_ready = false; }
BspRs485Status BSP_Rs485_StartSend(const uint8_t *data, uint16_t length)
{
    if (transport_send != BSP_RS485_OK) { return transport_send; }
    tx_pointer = data; tx_length = length; ++tx_count;
    transport_poll = BSP_RS485_BUSY;
    return BSP_RS485_OK;
}
static DevFlameConfig valid_config(void)
{
    DevFlameConfig c = {0}; /* 仅测试夹具，不是正式探头配置或阈值。 */
    c.sensitivity = 30;
    c.detection_enabled = true;
    c.interrupt_source = DEV_FLAME_INTERRUPT_EVENT;
    c.adc_source = DEV_FLAME_SOURCE_BPF;
    c.high_pass = DEV_FLAME_HPF_0P4_HZ;
    c.pulse_mode = DEV_FLAME_COUNT_ALTERNATING;
    return c;
}
static void reset_fakes(void)
{
    unsigned c;
    memset(pending_flags, 0, sizeof(pending_flags));
    memset(read_results, 0, sizeof(read_results));
    memset(pending_results, 0, sizeof(pending_results));
    memset(init_results, 0, sizeof(init_results));
    for (c = 0; c < 3; ++c) {
        memset(&samples[c], 0, sizeof(samples[c]));
        samples[c].adc_raw = (uint16_t)(1000 + c);
        samples[c].config_raw = 0x11110;
    }
    reads = inits = tx_count = 0;
    fake_now_ms = 0;
    require_latch = true;
    rx_ready = false;
    transport_poll = transport_send = BSP_RS485_OK;
}
static void ready_detection(AppFlameDetection *d, uint32_t now)
{
    AppFlameSetup setup[3];
    unsigned c;
    for (c = 0; c < 3; ++c) { setup[c].supplied = true; setup[c].detector = valid_config(); }
    observed_detection = d;
    fake_now_ms = now;
    AppFlameDetection_Init(d, setup, now, &fake_init_timing);
    assert(inits == 3);
    assert(AppFlameDetection_Poll(d, now + 555));
}
static void test_detection(void)
{
    AppFlameDetection d;
    AppFlameReport r = {0};
    DevFlameConfig c;
    unsigned index;
    reset_fakes();
    AppFlameDetection_Init(&d, NULL, 0, &fake_init_timing);
    assert(inits == 0 && reads == 0);
    AppFlameDetection_Snapshot(&d, 100, &r);
    assert(r.decision == APP_FLAME_UNKNOWN && r.channels[0].result == DEV_FLAME_NOT_READY);
    assert(r.channels[0].sample_age_ms == UINT32_MAX && r.channels[0].adc_raw == 0);
    for (index = 0; index < 3; ++index) {
        reset_fakes(); ready_detection(&d, 0);
        AppFlameDetection_Snapshot(&d, 555, &r);
        assert(r.decision == APP_FLAME_NOT_DETECTED && reads == 0);
        pending_flags[index] = true;
        assert(AppFlameDetection_Poll(&d, 600));
        AppFlameDetection_Snapshot(&d, 600, &r);
        assert(r.decision == APP_FLAME_TRIGGERED && r.channels[index].decision == APP_FLAME_TRIGGERED);
        assert(reads == 1 && r.channels[index].adc_raw == 1000 + index);
        assert((r.channels[index].flags & APP_FLAME_FLAG_BLIND) && r.channels[index].sample_age_ms == 0);
        assert(!AppFlameDetection_Poll(&d, 1000));
        assert(AppFlameDetection_Poll(&d, 1155));
        AppFlameDetection_Snapshot(&d, 1500, &r);
        assert(r.decision == APP_FLAME_TRIGGERED && r.channels[index].sample_age_ms == 900);
        assert(reads == 1); /* 普通周期不会重新读历史ADC。 */
    }
    reset_fakes(); ready_detection(&d, 0);
    samples[1].over_range = true; pending_flags[1] = true;
    AppFlameDetection_Poll(&d, 600); AppFlameDetection_Snapshot(&d, 600, &r);
    assert(r.decision == APP_FLAME_TRIGGERED && (r.channels[1].flags & APP_FLAME_FLAG_OVER_RANGE));
    assert(!(r.channels[1].flags & APP_FLAME_FLAG_SAMPLE_VALID));
    reset_fakes(); ready_detection(&d, 0);
    pending_flags[1] = true; read_results[1] = DEV_FLAME_TIMEOUT;
    AppFlameDetection_Poll(&d, 600); AppFlameDetection_Snapshot(&d, 600, &r);
    assert(r.decision == APP_FLAME_TRIGGERED && r.channels[1].result == DEV_FLAME_TIMEOUT);
    assert((r.channels[1].flags & APP_FLAME_FLAG_FAULT) && !(r.channels[1].flags & APP_FLAME_FLAG_HAS_SAMPLE));
    read_results[1] = DEV_FLAME_OK; pending_flags[1] = false; c = valid_config();
    assert(AppFlameDetection_Configure(&d, DEV_FLAME_U23, &c, 700) == DEV_FLAME_OK);
    assert(d.channels[1].triggered); AppFlameDetection_Poll(&d, 1255);
    AppFlameDetection_Snapshot(&d, 1255, &r);
    assert(r.decision == APP_FLAME_TRIGGERED && !(r.channels[1].flags & APP_FLAME_FLAG_FAULT));
    reset_fakes(); ready_detection(&d, 0);
    pending_flags[0] = true; AppFlameDetection_Poll(&d, 600);
    AppFlameDetection_Poll(&d, 1155); pending_flags[0] = true; read_results[0] = DEV_FLAME_CONFIG_MISMATCH;
    AppFlameDetection_Poll(&d, 1200); AppFlameDetection_Snapshot(&d, 1300, &r);
    assert(r.channels[0].adc_raw == 1000 && r.channels[0].sample_age_ms == 700);
    assert(r.channels[0].flags & APP_FLAME_FLAG_SAMPLE_VALID); /* 历史数据有效性与当前故障独立。 */
    reset_fakes(); ready_detection(&d, 0);
    pending_results[2] = DEV_FLAME_IO_ERROR;
    AppFlameDetection_Poll(&d, 600); AppFlameDetection_Snapshot(&d, 600, &r);
    assert(r.decision == APP_FLAME_UNKNOWN && !d.channels[2].triggered);
    c = valid_config(); c.interrupt_source = DEV_FLAME_INTERRUPT_ADC;
    assert(AppFlameDetection_Configure(&d, DEV_FLAME_U24, &c, 600) == DEV_FLAME_ARGUMENT_ERROR);
    reset_fakes(); ready_detection(&d, 0);
    pending_flags[0] = true; read_results[0] = DEV_FLAME_BUSY;
    AppFlameDetection_Poll(&d, 600);
    assert(d.channels[0].triggered && !d.channels[0].fault && d.channels[0].awaiting_sample);
    AppFlameDetection_Poll(&d, 700);
    assert(d.channels[0].fault && d.channels[0].result == DEV_FLAME_TIMEOUT);
    reset_fakes(); ready_detection(&d, UINT32_MAX - 600);
    pending_flags[2] = true; AppFlameDetection_Poll(&d, UINT32_MAX - 20);
    AppFlameDetection_Snapshot(&d, 29, &r);
    assert(r.channels[2].sample_age_ms == 50);
    d.channels[2].sample_age_ms = UINT32_MAX - 10;
    AppFlameDetection_Snapshot(&d, 49, &r);
    assert(r.channels[2].sample_age_ms == UINT32_MAX - 1);
    fake_now_ms = 0;
    AppFlameDetection_Init(&d, NULL, 0, &fake_init_timing);
    AppFlameDetection_Snapshot(&d, 0, &r);
    assert(r.decision == APP_FLAME_UNKNOWN && !d.channels[2].triggered); /* 模拟重启清零。 */
}
static void test_protocol(const char *vector_path)
{
    AppFlameReport r = {0}, decoded = {0};
    uint8_t frame[65];
    unsigned c;
    r.type = APP_FLAME_TYPE_CHANGE; r.address = 1; r.sequence = 0x1234;
    r.uptime_ms = 0x01020304; r.decision = APP_FLAME_TRIGGERED;
    for (c = 0; c < 3; ++c) {
        r.channels[c].channel_id = (uint8_t)(c + 1);
        r.channels[c].decision = APP_FLAME_UNKNOWN;
        r.channels[c].result = DEV_FLAME_NOT_READY;
        r.channels[c].flags = APP_FLAME_FLAG_FAULT;
        r.channels[c].sample_age_ms = UINT32_MAX;
    }
    r.channels[1].decision = APP_FLAME_TRIGGERED;
    r.channels[1].result = DEV_FLAME_TIMEOUT;
    r.channels[1].flags = APP_FLAME_FLAG_HAS_SAMPLE | APP_FLAME_FLAG_SAMPLE_VALID | APP_FLAME_FLAG_FAULT;
    r.channels[1].adc_raw = 0x1234; r.channels[1].config_raw = 0x1234567; r.channels[1].sample_age_ms = 200;
    assert(AppFlameFrame_Encode(&r, frame, sizeof(frame)));
    assert(frame[0] == 0xAA && frame[1] == 0x55 && frame[5] == 0x34 && frame[6] == 0x12);
    assert(frame[7] == 50 && frame[8] == 0 && frame[9] == 4 && frame[12] == 1);
    assert(frame[35] == 0xFC && frame[36] == 0xFF);
    assert(AppFlameFrame_Decode(frame, sizeof(frame), &decoded));
    assert(decoded.channels[1].result == DEV_FLAME_TIMEOUT && decoded.channels[1].adc_raw == 0x1234);
    assert(decoded.uptime_ms == r.uptime_ms && decoded.sequence == r.sequence);
    if (vector_path) { FILE *f = fopen(vector_path, "wb"); assert(f); assert(fwrite(frame, 1, 65, f) == 65); fclose(f); }
    frame[40] ^= 1;
    assert(!AppFlameFrame_Decode(frame, 65, &decoded));
    assert(decoded.sequence == r.sequence); /* 解码失败不修改输出。 */
    assert(!AppFlameFrame_Encode(&r, frame, 64));
    assert(!AppFlameFrame_Decode(frame, 64, &decoded));
    r.channels[0].adc_raw = 0x4000;
    assert(!AppFlameFrame_Encode(&r, frame, 65));
}
static DevModbusRegisterResult register_callback(void *context, uint8_t function, uint16_t address,
                                                uint16_t quantity, uint16_t *values)
{
    (void)context; assert(function == 3 && address == 0 && quantity == 1);
    values[0] = 0x1234; return DEV_MODBUS_REGISTER_OK;
}
static void test_transport(void)
{
    DevModbusSlave *dev = GetModbusSlave();
    uint8_t data[65], original[65];
    reset_fakes();
    memset(data, 0x55, sizeof(data)); memcpy(original, data, sizeof(data));
    assert(dev->init(dev, 1, NULL, NULL) == DEV_MODBUS_SLAVE_OK);
    assert(dev->sendRaw(dev, NULL, 65) == DEV_MODBUS_SLAVE_ERROR_ARGUMENT);
    assert(dev->sendRaw(dev, data, 0) == DEV_MODBUS_SLAVE_ERROR_ARGUMENT);
    assert(dev->sendRaw(dev, data, 257) == DEV_MODBUS_SLAVE_ERROR_ARGUMENT);
    assert(dev->sendRaw(dev, data, 65) == DEV_MODBUS_SLAVE_OK && tx_count == 1);
    assert(tx_pointer != data && tx_length == 65 && !memcmp(tx_pointer, original, 65));
    memset(data, 0x66, sizeof(data));
    assert(dev->sendRaw(dev, data, 65) == DEV_MODBUS_SLAVE_BUSY);
    assert(!memcmp(tx_pointer, original, 65));
    transport_poll = BSP_RS485_TIMEOUT;
    assert(dev->poll(dev) == DEV_MODBUS_SLAVE_ERROR_TIMEOUT);
    assert(dev->sendRaw(dev, data, 65) == DEV_MODBUS_SLAVE_NOT_INITIALIZED);
    assert(dev->init(dev, 1, NULL, NULL) == DEV_MODBUS_SLAVE_OK);
    memcpy(rx, "abcdefgh", 8); rx_length = 8; rx_ready = true;
    assert(dev->poll(dev) == DEV_MODBUS_SLAVE_IDLE && !rx_ready && tx_count == 1);
    assert(dev->init(dev, 1, register_callback, NULL) == DEV_MODBUS_SLAVE_OK);
    assert(dev->sendRaw(dev, data, 65) == DEV_MODBUS_SLAVE_ERROR_ARGUMENT);
    { const uint8_t request[] = {1, 3, 0, 0, 0, 1, 0x84, 0x0A};
      memcpy(rx, request, 8); rx_length = 8; rx_ready = true; }
    assert(dev->poll(dev) == DEV_MODBUS_SLAVE_OK && tx_count == 2);
    assert(tx_length == 7 && tx_pointer[0] == 1 && tx_pointer[1] == 3 && tx_pointer[2] == 2);
    assert(tx_pointer[3] == 0x12 && tx_pointer[4] == 0x34);
    transport_poll = BSP_RS485_OK; rx[7] ^= 1; rx_ready = true;
    assert(dev->poll(dev) == DEV_MODBUS_SLAVE_IDLE && tx_count == 2);
}
static void test_scheduler(void)
{
    AppFlameReport decoded;
    DevFlameConfig c = valid_config();
    uint32_t started_ms;
    reset_fakes(); require_latch = false;
    /* 启用配置后模拟三路初始化失败，保留未知及通信恢复场景的覆盖。 */
    init_results[0] = init_results[1] = init_results[2] = DEV_FLAME_IO_ERROR;
    assert(App_Main_Init(0, &fake_init_timing) == DEV_MODBUS_SLAVE_OK);
    started_ms = fake_now_ms; /* 后续正常调度从同步启动重试结束时开始。 */
    App_Main_Poll(started_ms); assert(tx_count == 1);
    assert(AppFlameFrame_Decode(tx_pointer, tx_length, &decoded));
    assert(decoded.decision == APP_FLAME_UNKNOWN && decoded.sequence == 0);
    transport_poll = BSP_RS485_OK;
    App_Main_Poll(started_ms + 999); assert(tx_count == 1);
    App_Main_Poll(started_ms + 1000); assert(tx_count == 2);
    transport_poll = BSP_RS485_OK;
    init_results[0] = DEV_FLAME_OK;
    assert(App_Main_ConfigureFlame(DEV_FLAME_U22, &c, started_ms + 1000) == DEV_FLAME_OK);
    App_Main_Poll(started_ms + 1099); assert(tx_count == 2);
    App_Main_Poll(started_ms + 1100); assert(tx_count == 3);
    transport_poll = BSP_RS485_OK;
    App_Main_Poll(started_ms + 1560); assert(tx_count == 4); /* 盲锁结束，可用性变化。 */
    transport_poll = BSP_RS485_OK;
    pending_flags[0] = true;
    App_Main_Poll(started_ms + 1570); assert(tx_count == 4); /* 先保存事件但守住100ms间隔。 */
    App_Main_Poll(started_ms + 1659); assert(tx_count == 4);
    App_Main_Poll(started_ms + 1660); assert(tx_count == 5);
    assert(AppFlameFrame_Decode(tx_pointer, tx_length, &decoded));
    assert(decoded.decision == APP_FLAME_TRIGGERED && decoded.type == APP_FLAME_TYPE_CHANGE);
    assert(decoded.channels[0].adc_raw == 1000 && decoded.channels[0].sample_age_ms == 90);
    transport_poll = BSP_RS485_TIMEOUT;
    App_Main_Poll(started_ms + 1670);
    assert(g_app_communication_status == DEV_MODBUS_SLAVE_ERROR_TIMEOUT);
    App_Main_Poll(started_ms + 2670); assert(tx_count == 6);
    assert(AppFlameFrame_Decode(tx_pointer, tx_length, &decoded) && decoded.decision == APP_FLAME_TRIGGERED);
}
static void test_enabled_startup(void)
{
    AppFlameReport decoded;
    reset_fakes(); require_latch = false;
    assert(App_Main_Init(0, &fake_init_timing) == DEV_MODBUS_SLAVE_OK);
    assert(inits == 3 && reads == 0); /* 正式配置确实启动三路，初始化不消费火焰事件。 */
    App_Main_Poll(0);
    assert(AppFlameFrame_Decode(tx_pointer, tx_length, &decoded));
    assert(decoded.decision == APP_FLAME_UNKNOWN); /* 初始化后的盲锁期不能输出未检出。 */
    transport_poll = BSP_RS485_OK;
    App_Main_Poll(560);
    assert(AppFlameFrame_Decode(tx_pointer, tx_length, &decoded));
    assert(decoded.decision == APP_FLAME_NOT_DETECTED);
    assert(decoded.channels[0].flags == APP_FLAME_FLAG_DETECTION_READY);
    assert(decoded.channels[1].result == DEV_FLAME_OK && reads == 0);
    transport_poll = BSP_RS485_OK;
    pending_flags[2] = true;
    App_Main_Poll(570);
    App_Main_Poll(660);
    assert(AppFlameFrame_Decode(tx_pointer, tx_length, &decoded));
    assert(decoded.decision == APP_FLAME_TRIGGERED);
    assert(decoded.channels[2].adc_raw == 1002 && reads == 1);
}
int main(int argc, char **argv)
{
    test_detection(); puts("PASS detection: independent events, latch-before-read, faults, blind, history, recovery, wrap, restart");
    test_protocol(argc > 1 ? argv[1] : NULL); puts("PASS protocol: 65-byte layout, little endian, signed result, CRC rejection");
    test_transport(); puts("PASS transport: raw exclusivity, stable async buffer, busy, timeout, RX discard, Modbus regression");
    test_scheduler(); puts("PASS scheduler: startup unknown, 1s period, 100ms minimum, change frames, communication recovery");
    test_enabled_startup(); puts("PASS enabled startup: all three initialized, blind unknown, ready no-event, real event capture");
    return 0;
}
