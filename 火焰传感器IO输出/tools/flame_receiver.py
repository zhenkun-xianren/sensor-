"""火焰分钟事件计数、初始化及清除结果接收器；支持历史检测帧，串口接收另需pyserial。"""
import argparse
import json
import struct
import time

FRAME_BYTES = 65
PAYLOAD_BYTES = 50
MINUTE_PERIODIC_TYPE = 6  # 本分钟计数周期报告。
MINUTE_CHANGE_TYPE = 7  # 本分钟计数或诊断状态变化报告。
MINUTE_TYPES = (MINUTE_PERIODIC_TYPE, MINUTE_CHANGE_TYPE)  # 新计数帧类型集合。
MINUTE_FRAME_BYTES = 80  # 三路固定分钟报告的总字节数。
MINUTE_PAYLOAD_BYTES = 65  # 开机时间之后、CRC之前的载荷字节数。
MINUTE_INDEX_OFFSET = 13  # 从APP初始化起计的分钟编号，首分钟为0。
MINUTE_ELAPSED_OFFSET = 17  # 本分钟已过毫秒数。
MINUTE_CHANNELS_OFFSET = 21  # 三路19字节通道块起点。
MINUTE_CHANNEL_BYTES = 19  # 每个通道块长度。
MINUTE_CRC_OFFSET = 78  # 分钟帧CRC起点。
MINUTE_PERIOD_MS = 60000  # 固定窗口长度，不按墙上时钟分段。
MILLISECONDS_PER_SECOND = 1000  # 显示秒数所用的单位换算。
PROTOCOL_VERSION = 1  # 新计数帧沿用公共协议版本。
VERSION_OFFSET = 2  # 公共帧头中的版本字节。
TYPE_OFFSET = 3  # 公共帧头中的消息类型字节。
ADDRESS_OFFSET = 4  # 公共帧头中的设备地址字节。
SEQUENCE_OFFSET = 5  # 公共帧头中的16位序号起点。
LENGTH_OFFSET = 7  # 公共帧头中的16位载荷长度起点。
UPTIME_OFFSET = 9  # 公共帧头之后的开机毫秒数起点。
CHANNEL_ID_BASE = 1  # 线上通道号从1开始。
FLAG_READY = 1 << 0  # 本路当前检测可用。
FLAG_HAS_SAMPLE = 1 << 1  # 保留了事件采样数据。
FLAG_SAMPLE_VALID = 1 << 2  # 保留的原始数据有效且未超范围。
FLAG_OVER_RANGE = 1 << 3  # 保留的原始数据超范围。
FLAG_FAULT = 1 << 4  # 当前配置或驱动硬故障。
FLAG_BLIND = 1 << 5  # 当前处于正常保守盲锁期。
FLAG_MINUTE_INCOMPLETE = 1 << 6  # 本分钟曾未初始化、硬故障或人工清零。
MINUTE_KNOWN_FLAGS = 0x7F  # 新计数帧允许的状态位。
ADC_RAW_MAX = 0x3FFF  # 原始ADC字段上限。
CONFIG_RAW_MAX = 0x1FFFFFF  # 配置回读字段上限。
SOURCE_SHIFT = 5  # 配置字段中数据源起始位。
SOURCE_MASK = 3  # 两位数据源的掩码。
SOURCE_NAMES = ('BPF', 'LPF', 'SUPPLY', 'TEMPERATURE')  # 数据源名称对应关系。
INITIALIZATION_TYPE = 4  # 独立初始化状态消息类型。
INITIALIZATION_FRAME_BYTES = 47  # 初始化状态帧总长度，单位字节。
INITIALIZATION_PAYLOAD_BYTES = 32  # 开机时间之后的初始化载荷长度。
INIT_RETRY_LIMIT_OFFSET = 13  # 全局额外重试上限字段位置。
INIT_RETRY_INTERVAL_OFFSET = 17  # 重试间隔毫秒字段位置。
INIT_CHANNELS_OFFSET = 21  # 三路初始化通道块的起点。
INIT_CHANNEL_BYTES = 8  # 每路初始化通道块长度。
INIT_CRC_OFFSET = 45  # 初始化帧CRC起点。
CLEAR_RESULT_TYPE = 5  # 独立清除结果消息类型。
CLEAR_RESULT_FRAME_BYTES = 26  # 固定清除结果帧长度。
CLEAR_RESULT_PAYLOAD_BYTES = 11  # 开机时间之后的清除结果载荷长度。
CLEAR_REQUEST_SEQUENCE_OFFSET = 13  # 原始清除命令序号位置。
CLEAR_SUCCESS_OFFSET = 15  # 总体清除成功或失败编码位置。
CLEAR_NOT_INITIALIZED_OFFSET = 16  # 未初始化成功通道位图位置。
CLEAR_FAILED_OFFSET = 17  # 已尝试清除但失败的通道位图位置。
CLEAR_RESULTS_OFFSET = 18  # U22/U23/U24三个有符号结果码位置。
CLEAR_RESULT_CRC_OFFSET = 24  # 清除结果帧CRC起点。
CLEAR_CHANNEL_MASK = 0x07  # 三个通道的有效位图范围。
CLEAR_CHANNEL_BIT = 1  # 各通道位图的基础位。
CLEAR_NO_CHANNELS = 0  # 没有未初始化或失败通道。
CLEAR_FAILURE = 0  # 总体清除失败的线上编码。
CLEAR_SUCCESS = 1  # 三路实际清除成功的线上编码。
CLEAR_RESULT_WORD_BYTES = 2  # 单个有符号清除结果的字节长度。
CLEAR_CHANNEL_NAMES = ('U22', 'U23', 'U24')  # 两个位图和结果数组共用固定通道顺序。
FRAME_LENGTHS = {1: FRAME_BYTES, 2: FRAME_BYTES, INITIALIZATION_TYPE: INITIALIZATION_FRAME_BYTES,
                 CLEAR_RESULT_TYPE: CLEAR_RESULT_FRAME_BYTES,
                 MINUTE_PERIODIC_TYPE: MINUTE_FRAME_BYTES,
                 MINUTE_CHANGE_TYPE: MINUTE_FRAME_BYTES}  # 按消息类型选取帧长。
INIT_NOT_CONFIGURED = 0  # 本路尚未收到配置。
INIT_SUCCESS = 1  # 最近一轮初始化及回读核对成功。
INIT_RETRYING = 2  # 配置未成功且还有待执行的重试。
INIT_FAILED = 3  # 本轮失败且已停止重试。
RESULT_OK = 0  # 配置初始化成功结果码。
RESULT_NOT_READY = -2  # 未提供配置时的结果码。
RESULT_ARGUMENT_ERROR = -1  # 设备接口或参数无效。
RESULT_BUSY = -3  # 清除时底层资源忙。
RESULT_TIMEOUT = -4  # 清除操作超时。
RESULT_CONFIG_MISMATCH = -5  # 设备配置回读不匹配。
RESULT_IO_ERROR = -6  # 板级输入输出错误。
CLEAR_RESULT_MEANINGS = {RESULT_OK: '清除成功', RESULT_NOT_READY: '设备未就绪',
                        RESULT_ARGUMENT_ERROR: '接口或参数无效', RESULT_BUSY: '资源忙',
                        RESULT_TIMEOUT: '清除超时', RESULT_CONFIG_MISMATCH: '配置不匹配',
                        RESULT_IO_ERROR: '板级输入输出错误'}  # 各路清除结果的中文说明。
INIT_STATUSES = {INIT_NOT_CONFIGURED: '未配置', INIT_SUCCESS: '初始化成功', INIT_RETRYING: '等待重试', INIT_FAILED: '初始化失败'}  # 初始化状态中文说明。
HEADER = b'\xaa\x55'
HALF_FRAME_TIMEOUT_MS = 200
OFFLINE_TIMEOUT_MS = 3000
UNKNOWN = 2
NO_SAMPLE_AGE = 0xFFFFFFFF
DECISIONS = {0: '未检出火焰事件', 1: '内置火焰事件已触发（锁存）', 2: '无法判断'}


def crc16(data):
    """沿用项目CRC参数，结果按低字节在前编码。"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


def decode_frame(frame):
    """@brief 解析分钟、历史检测、初始化或清除结果帧，拒绝CRC及字段无效的数据。
    @param frame 收到的完整二进制帧。
    @return 解码后的报告字典；无效帧抛出ValueError。
    """
    if len(frame) > 3 and frame[:2] == HEADER and frame[3] in MINUTE_TYPES:
        return decode_minute_frame(frame)
    if len(frame) > 3 and frame[:2] == HEADER and frame[3] == INITIALIZATION_TYPE:
        return decode_initialization_frame(frame)
    if len(frame) > 3 and frame[:2] == HEADER and frame[3] == CLEAR_RESULT_TYPE:
        return decode_clear_result_frame(frame)
    if len(frame) != FRAME_BYTES or frame[:2] != HEADER:
        raise ValueError('帧头或长度错误')
    if frame[2] != 1 or frame[3] not in (1, 2) or frame[14] != 1:
        raise ValueError('不支持的版本、消息类型或判断方式')
    if struct.unpack_from('<H', frame, 7)[0] != PAYLOAD_BYTES:
        raise ValueError('载荷长度错误')
    if crc16(frame[:63]) != struct.unpack_from('<H', frame, 63)[0]:
        raise ValueError('CRC错误')
    if frame[13] not in DECISIONS:
        raise ValueError('检测结果错误')
    channels = []
    for index in range(3):
        channel, decision, flags, result, adc, config, age = struct.unpack_from('<BBHhHII', frame, 15 + 16 * index)
        if channel != index + 1 or decision not in DECISIONS or flags & ~0x3F or adc > 0x3FFF or config > 0x1FFFFFF:
            raise ValueError('通道字段错误')
        has_sample = bool(flags & 2)
        channels.append({
            'channel': ('U22', 'U23', 'U24')[index],
            'decision': decision, 'meaning': DECISIONS[decision],
            'detection_ready': bool(flags & 1), 'has_sample': has_sample,
            'sample_valid': bool(flags & 4), 'over_range': bool(flags & 8),
            'fault': bool(flags & 16), 'blind': bool(flags & 32),
            'result': result, 'adc_raw': adc if has_sample else None,
            'config_raw': config if has_sample else None,
            'source': ('BPF', 'LPF', 'SUPPLY', 'TEMPERATURE')[(config >> 5) & 3] if has_sample else None,
            'sample_age_ms': age if age != NO_SAMPLE_AGE else None,
        })
    return {
        'version': frame[2], 'type': frame[3], 'address': frame[4],
        'sequence': struct.unpack_from('<H', frame, 5)[0],
        'uptime_ms': struct.unpack_from('<I', frame, 9)[0],
        'decision': frame[13], 'meaning': DECISIONS[frame[13]],
        'method': '探头内置检测', 'channels': channels,
    }


def minute_summary(report):
    """@brief 根据设备快照显示本分钟三路次数及故障、盲锁、统计不完整提示。
    @param report 已验证的分钟计数报告。
    @return 中文次数摘要，不把三路次数之和解释为火焰数量。
    """
    descriptions = []  # 三路独立次数及状态提示。
    for channel in report['channels']:  # 当前通道显示对象。
        notes = []  # 与本路次数同时显示的诊断说明。
        if channel['fault']:
            notes.append('故障')
        if channel['blind']:
            notes.append('盲锁中')
        if not channel['detection_ready'] and not channel['fault'] and not channel['blind']:
            notes.append('当前检测不可用')
        if channel['minute_incomplete']:
            notes.append('本分钟统计不完整')
        suffix = '（' + '、'.join(notes) + '）' if notes else ''  # 本路诊断状态的可读后缀。
        descriptions.append(f"{channel['channel']}={channel['event_count']}次{suffix}")
    seconds = report['minute_elapsed_ms'] // MILLISECONDS_PER_SECOND  # 只显示设备已统计到的秒数。
    return f"第{report['minute_index'] + 1}分钟，已过{seconds}秒：" + '，'.join(descriptions)


def decode_minute_frame(frame):
    """@brief 解码80字节分钟计数帧，保持次数、窗口和诊断字段的设备原始语义。
    @param frame 完整80字节二进制帧。
    @return 分钟报告字典；无效帧抛出ValueError。
    """
    if len(frame) != MINUTE_FRAME_BYTES or frame[:2] != HEADER:
        raise ValueError('分钟帧头或长度错误')
    if frame[VERSION_OFFSET] != PROTOCOL_VERSION or frame[TYPE_OFFSET] not in MINUTE_TYPES:
        raise ValueError('分钟帧版本或消息类型错误')
    if struct.unpack_from('<H', frame, LENGTH_OFFSET)[0] != MINUTE_PAYLOAD_BYTES:
        raise ValueError('分钟帧载荷长度错误')
    if crc16(frame[:MINUTE_CRC_OFFSET]) != struct.unpack_from('<H', frame, MINUTE_CRC_OFFSET)[0]:
        raise ValueError('分钟帧CRC错误')
    elapsed = struct.unpack_from('<I', frame, MINUTE_ELAPSED_OFFSET)[0]  # 本窗口已过毫秒数。
    if elapsed >= MINUTE_PERIOD_MS:
        raise ValueError('分钟帧时间范围错误')
    channels = []  # 按U22、U23、U24保存独立计数和诊断数据。
    for index, name in enumerate(CLEAR_CHANNEL_NAMES):  # 当前通道索引及位号。
        channel, flags, result, adc, config, age, count = struct.unpack_from('<BHhHIII', frame, MINUTE_CHANNELS_OFFSET + MINUTE_CHANNEL_BYTES * index)  # 本路19字节字段。
        if channel != index + CHANNEL_ID_BASE or flags & ~MINUTE_KNOWN_FLAGS or adc > ADC_RAW_MAX or config > CONFIG_RAW_MAX:
            raise ValueError('分钟帧通道字段错误')
        has_sample = bool(flags & FLAG_HAS_SAMPLE)  # 判断采样字段是历史数据还是占位。
        channels.append({
            'channel': name, 'event_count': count,
            'detection_ready': bool(flags & FLAG_READY), 'has_sample': has_sample,
            'sample_valid': bool(flags & FLAG_SAMPLE_VALID), 'over_range': bool(flags & FLAG_OVER_RANGE),
            'fault': bool(flags & FLAG_FAULT), 'blind': bool(flags & FLAG_BLIND),
            'minute_incomplete': bool(flags & FLAG_MINUTE_INCOMPLETE),
            'result': result, 'adc_raw': adc if has_sample else None,
            'config_raw': config if has_sample else None,
            'source': SOURCE_NAMES[(config >> SOURCE_SHIFT) & SOURCE_MASK] if has_sample else None,
            'sample_age_ms': age if age != NO_SAMPLE_AGE else None,
        })
    report = {  # 已验证的设备快照，不根据接收帧数量推算事件次数。
        'version': frame[VERSION_OFFSET], 'type': frame[TYPE_OFFSET], 'address': frame[ADDRESS_OFFSET],
        'sequence': struct.unpack_from('<H', frame, SEQUENCE_OFFSET)[0],
        'uptime_ms': struct.unpack_from('<I', frame, UPTIME_OFFSET)[0],
        'minute_report': True, 'minute_period_ms': MINUTE_PERIOD_MS,
        'minute_index': struct.unpack_from('<I', frame, MINUTE_INDEX_OFFSET)[0],
        'minute_elapsed_ms': elapsed, 'channels': channels,
    }
    report['meaning'] = minute_summary(report)
    return report


def decode_initialization_frame(frame):
    """@brief 解析初始化状态、最后配置结果及实际重试次数。
    @param frame 完整47字节初始化帧。
    @return 独立初始化报告字典；无效帧抛出ValueError。
    """
    if len(frame) != INITIALIZATION_FRAME_BYTES or frame[:2] != HEADER:
        raise ValueError('初始化帧头或长度错误')
    if frame[2] != 1 or frame[3] != INITIALIZATION_TYPE:
        raise ValueError('初始化帧版本或消息类型错误')
    if struct.unpack_from('<H', frame, 7)[0] != INITIALIZATION_PAYLOAD_BYTES:
        raise ValueError('初始化帧载荷长度错误')
    if crc16(frame[:INIT_CRC_OFFSET]) != struct.unpack_from('<H', frame, INIT_CRC_OFFSET)[0]:
        raise ValueError('初始化帧CRC错误')
    retry_limit = struct.unpack_from('<I', frame, INIT_RETRY_LIMIT_OFFSET)[0]  # 本机额外重试次数上限。
    retry_interval = struct.unpack_from('<I', frame, INIT_RETRY_INTERVAL_OFFSET)[0]  # 两次尝试之间的间隔毫秒数。
    channels = []  # 三路初始化结果，不使用检测帧的临时查询结果码。
    for index in range(3):  # 当前初始化通道索引，按U22/U23/U24排列。
        channel, status, result, retries = struct.unpack_from('<BBhI', frame, INIT_CHANNELS_OFFSET + INIT_CHANNEL_BYTES * index)  # 通道号、初始化状态、配置结果及重试次数。
        if channel != index + 1 or status not in INIT_STATUSES or retries > retry_limit:
            raise ValueError('初始化通道字段错误')
        if (status == INIT_SUCCESS and result != RESULT_OK) or (status == INIT_RETRYING and (result == RESULT_OK or retries >= retry_limit)):
            raise ValueError('初始化状态与结果或重试次数不一致')
        if status == INIT_NOT_CONFIGURED and (result != RESULT_NOT_READY or retries):
            raise ValueError('未配置通道字段不一致')
        channels.append({
            'channel': ('U22', 'U23', 'U24')[index],
            'initialization_status': status, 'meaning': INIT_STATUSES[status],
            'initialization_success': status == INIT_SUCCESS,
            'retry_pending': status == INIT_RETRYING,
            'retries_done': retries, 'retries_remaining': retry_limit - retries if status == INIT_RETRYING else 0,
            'config_result': result,
        })
    return {
        'version': frame[2], 'type': frame[3], 'address': frame[4],
        'sequence': struct.unpack_from('<H', frame, 5)[0],
        'uptime_ms': struct.unpack_from('<I', frame, 9)[0],
        'initialization_report': True,
        'all_initialization_success': all(channel['initialization_success'] for channel in channels),
        'retry_limit': retry_limit, 'retry_interval_ms': retry_interval,
        'channels': channels,
    }


def decode_clear_result_frame(frame):
    """@brief 解析一次清除命令的固定结果，区分未初始化通道与已尝试清除失败通道。
    @param frame 完整26字节清除结果帧。
    @return 包含总体结果、未初始化及失败名单、各路结果码的独立字典；无效帧抛出ValueError。
    """
    if len(frame) != CLEAR_RESULT_FRAME_BYTES or frame[:2] != HEADER:
        raise ValueError('清除结果帧头或长度错误')
    if frame[2] != 1 or frame[3] != CLEAR_RESULT_TYPE:
        raise ValueError('清除结果版本或消息类型错误')
    if struct.unpack_from('<H', frame, 7)[0] != CLEAR_RESULT_PAYLOAD_BYTES:
        raise ValueError('清除结果载荷长度错误')
    if crc16(frame[:CLEAR_RESULT_CRC_OFFSET]) != struct.unpack_from('<H', frame, CLEAR_RESULT_CRC_OFFSET)[0]:
        raise ValueError('清除结果CRC错误')
    success = frame[CLEAR_SUCCESS_OFFSET]  # 三路实际清除是否全部成功。
    not_initialized = frame[CLEAR_NOT_INITIALIZED_OFFSET]  # 本次清除时未初始化成功通道。
    failed = frame[CLEAR_FAILED_OFFSET]  # 本次已经调用清除接口但失败的通道。
    affected = not_initialized | failed  # 不能确认清除成功的全部通道。
    if success not in (CLEAR_FAILURE, CLEAR_SUCCESS) or affected & ~CLEAR_CHANNEL_MASK or not_initialized & failed:
        raise ValueError('清除结果或通道位图错误')
    if (success == CLEAR_SUCCESS) != (affected == CLEAR_NO_CHANNELS):
        raise ValueError('总体清除结果与通道位图不一致')
    channels = []  # 固定U22/U23/U24顺序的清除结果。
    for index, name in enumerate(CLEAR_CHANNEL_NAMES):  # 当前通道位和中文位号。
        channel_bit = CLEAR_CHANNEL_BIT << index  # 本路位图掩码。
        result = struct.unpack_from('<h', frame, CLEAR_RESULTS_OFFSET + CLEAR_RESULT_WORD_BYTES * index)[0]  # 16位有符号错误码。
        uninitialized = bool(not_initialized & channel_bit)  # 本次是否因为未初始化而跳过清除。
        clear_failed = bool(failed & channel_bit)  # 本次是否实际尝试清除但未成功。
        if ((uninitialized and result != RESULT_NOT_READY) or
                (clear_failed and result == RESULT_OK) or
                (not uninitialized and not clear_failed and result != RESULT_OK)):
            raise ValueError('通道清除结果与位图不一致')
        channels.append({
            'channel': name, 'not_initialized': uninitialized, 'clear_failed': clear_failed,
            'clear_success': not uninitialized and not clear_failed,
            'result': result,
            'meaning': '未初始化成功，未执行清除' if uninitialized else CLEAR_RESULT_MEANINGS.get(result, '清除未成功'),
        })
    return {
        'version': frame[2], 'type': frame[3], 'address': frame[4],
        'sequence': struct.unpack_from('<H', frame, 5)[0],
        'uptime_ms': struct.unpack_from('<I', frame, 9)[0],
        'request_sequence': struct.unpack_from('<H', frame, CLEAR_REQUEST_SEQUENCE_OFFSET)[0],
        'clear_result_report': True, 'clear_success': success == CLEAR_SUCCESS,
        'meaning': '三路清除成功' if success == CLEAR_SUCCESS else '清除失败，请查看未初始化或清除失败通道',
        'not_initialized_mask': not_initialized, 'failed_mask': failed,
        'not_initialized_channels': [channel['channel'] for channel in channels if channel['not_initialized']],
        'failed_channels': [channel['channel'] for channel in channels if channel['clear_failed']],
        'channels': channels,
    }


class FlameReceiver:
    """接收一个选定地址；损坏数据不刷新在线状态。"""
    def __init__(self, address=1):
        """@brief 建立混合帧接收器，独立保存分钟次数、历史检测、初始化和清除结果。
        @param self 本接收器对象。
        @param address 要接收的设备地址。
        """
        self.address = address
        self.buffer = bytearray()
        self.partial_since_ms = None
        self.last_report = None
        self.last_minute = None  # 最近设备分钟快照，不由历史检测或初始化帧替代。
        self.last_minute_ms = None  # 最近有效分钟快照的接收时刻，独立判断统计新鲜度。
        self.last_frame = None  # 三种消息共用序号，按最近完整帧检查丢帧及重启。
        self.last_initialization = None  # 最近独立初始化报告，独立于检测结果。
        self.last_clear_result = None  # 最近一次清除命令的独立结果，不作为实时检测快照。
        self.last_clear_result_ms = None  # 最近清除结果接收时刻。
        self.last_valid_ms = None
        self.last_detection_ms = None  # 最近有效检测状态的接收时刻，初始化帧不能延长检测结果有效期。
        self.last_initialization_ms = None  # 最近有效初始化状态的接收时刻。

    def expire_partial(self, now_ms):
        if self.partial_since_ms is not None and now_ms - self.partial_since_ms >= HALF_FRAME_TIMEOUT_MS:
            self.buffer.clear()
            self.partial_since_ms = None

    def feed(self, data, now_ms):
        """@brief 解析混合完整帧，维护共用序号和独立分钟缓存，旧帧不刷新分钟统计。
        @param self 本接收器对象。
        @param data 本次收到的二进制字节串。
        @param now_ms 接收端当前单调毫秒时间。
        @return 本次成功解析且地址匹配的报告列表。
        """
        self.expire_partial(now_ms)
        self.buffer.extend(data)
        reports = []
        while self.buffer:
            start = self.buffer.find(HEADER)
            if start < 0:
                self.buffer[:] = self.buffer[-1:] if self.buffer[-1:] == HEADER[:1] else b''
                break
            if start:
                del self.buffer[:start]
                self.partial_since_ms = None
            if len(self.buffer) < 4:
                break
            frame_bytes = FRAME_LENGTHS.get(self.buffer[3])  # 从已收到的消息类型确定长度，避免等待65字节才解析短帧。
            if frame_bytes is None:
                del self.buffer[0]
                self.partial_since_ms = None
                continue
            if len(self.buffer) < frame_bytes:
                break
            try:
                report = decode_frame(bytes(self.buffer[:frame_bytes]))
            except ValueError:
                del self.buffer[0]
                self.partial_since_ms = None
                continue
            del self.buffer[:frame_bytes]
            self.partial_since_ms = None
            if report['address'] != self.address:
                continue
            report['lost_frames'] = 0
            report['restarted'] = False
            report['duplicate'] = False
            if self.last_frame is not None:
                previous = self.last_frame
                natural_wrap = previous['uptime_ms'] >= 0xF0000000 and report['uptime_ms'] < 0x10000000
                report['restarted'] = report['uptime_ms'] < previous['uptime_ms'] and not natural_wrap
                if not report['restarted']:
                    delta = (report['sequence'] - previous['sequence']) & 0xFFFF
                    report['duplicate'] = delta == 0
                    report['lost_frames'] = max(0, delta - 1)
            if report['restarted']:
                self.last_report = None
                self.last_detection_ms = None
                self.last_minute = None
                self.last_minute_ms = None
                self.last_initialization = None
                self.last_initialization_ms = None
                self.last_clear_result = None
                self.last_clear_result_ms = None
            self.last_frame = report
            if report.get('minute_report', False):
                self.last_minute = report
                self.last_minute_ms = now_ms
            elif report.get('clear_result_report', False):
                self.last_clear_result = report
                self.last_clear_result_ms = now_ms
            elif report.get('initialization_report', False):
                self.last_initialization = report
                self.last_initialization_ms = now_ms
            else:
                self.last_report = report
                self.last_detection_ms = now_ms
            self.last_valid_ms = now_ms
            reports.append(report)
        if self.buffer and self.partial_since_ms is None:
            self.partial_since_ms = now_ms
        return reports

    def status(self, now_ms):
        """@brief 按独立新鲜度显示分钟次数，故障保留诊断、超时返回不可用而非零次。
        @param self 本接收器对象。
        @param now_ms 接收端当前单调毫秒时间。
        @return 在线状态、分钟次数快照、初始化及清除结果，历史检测仅供回看。
        """
        self.expire_partial(now_ms)
        online = self.last_valid_ms is not None and now_ms - self.last_valid_ms < OFFLINE_TIMEOUT_MS  # 任一有效本机帧是否保持通信在线。
        minute_current = online and self.last_minute_ms is not None and now_ms - self.last_minute_ms < OFFLINE_TIMEOUT_MS  # 只有分钟帧刷新次数有效期。
        initialization_current = online and self.last_initialization_ms is not None and now_ms - self.last_initialization_ms < OFFLINE_TIMEOUT_MS  # 判断初始化状态的新鲜度。
        counts = {channel['channel']: channel['event_count'] for channel in self.last_minute['channels']} if minute_current else None  # 超时或未接收到分钟帧时不伪装成0次。
        return {
            'online': online,
            'minute_current': minute_current, 'minute_counts': counts,
            'minute_index': self.last_minute['minute_index'] if minute_current else None,
            'minute_elapsed_ms': self.last_minute['minute_elapsed_ms'] if minute_current else None,
            'meaning': self.last_minute['meaning'] if minute_current else '分钟次数超时或尚未收到计数数据，统计不可用',
            'reported_minute': self.last_minute,
            'legacy_detection': self.last_report,
            'initialization_current': initialization_current,
            'initialization': self.last_initialization,
            'clear_result': self.last_clear_result,
            'clear_result_received_ms': self.last_clear_result_ms,
        }


def main():
    """@brief 解析离线字节串或由用户执行串口接收，显示本分钟次数及独立诊断结果。"""
    parser = argparse.ArgumentParser(description=__doc__)  # 命令行参数解析器。
    source = parser.add_mutually_exclusive_group(required=True)  # 用户选择离线输入或实际串口。
    source.add_argument('--hex', help='离线解析十六进制字节串')
    source.add_argument('--port', help='用户执行时打开的串口，例如COM3；需要pyserial')
    parser.add_argument('--address', type=int, default=1)
    args = parser.parse_args()  # 保存用户指定的数据来源与设备地址。
    receiver = FlameReceiver(args.address)  # 按设备地址接收混合帧并独立维护分钟有效期。
    if args.hex:
        for report in receiver.feed(bytes.fromhex(args.hex), 0):
            print(json.dumps(report, ensure_ascii=False))
        print(json.dumps(receiver.status(0), ensure_ascii=False))
        return
    try:
        import serial
    except ImportError:
        parser.error('实际串口接收需要pyserial；离线--hex模式只依赖Python标准库')
    with serial.Serial(args.port, 9600, bytesize=8, parity='N', stopbits=1, timeout=0.05) as port:
        previous_status = None  # 最近已显示的状态，用于减少重复输出。
        while True:
            data = port.read(port.in_waiting or 1)  # 当前串口接收的字节块。
            now_ms = int(time.monotonic() * MILLISECONDS_PER_SECOND)  # 接收端单调毫秒时间。
            for report in receiver.feed(data, now_ms):
                print(json.dumps(report, ensure_ascii=False), flush=True)
            status = receiver.status(now_ms)  # 仅有效分钟帧提供次数，超时保持不可用提示。
            if status != previous_status:
                print(json.dumps(status, ensure_ascii=False), flush=True)
                previous_status = status


if __name__ == '__main__':
    main()
