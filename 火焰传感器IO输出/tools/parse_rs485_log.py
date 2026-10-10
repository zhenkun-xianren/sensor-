"""拖放解析RS485十六进制日志，并按当前帧协议显示中文摘要。"""
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

from flame_receiver import FRAME_LENGTHS, HEADER, decode_frame

BYTE_PATTERN = re.compile(r"(?<![0-9A-Fa-f])[0-9A-Fa-f]{2}(?![0-9A-Fa-f])")
FRAME_TYPE_NAMES = {
    1: "历史周期检测",
    2: "历史变化检测",
    4: "初始化状态",
    5: "清除结果",
    6: "分钟次数周期报告",
    7: "分钟次数变化报告",
}
CHANNEL_NAMES = ("U22", "U23", "U24")
MAX_REPORTED_FRAGMENTS = 8


def read_hex_file(file_path):
    """@brief 读取文本日志并提取两位十六进制字节。
    @param file_path 待读取的日志文件路径。
    @return 提取出的字节序列及提取数量。
    """
    raw_text = file_path.read_text(encoding="utf-8-sig", errors="replace")
    tokens = BYTE_PATTERN.findall(raw_text)
    return bytes(int(token, 16) for token in tokens), len(tokens)


def split_frames(data):
    """@brief 按帧头和项目帧长分割字节流并校验完整帧。
    @param data 从日志中提取的原始字节。
    @return 有效帧、无效帧说明、帧外字节及未完成尾帧。
    """
    valid_frames = []  # 保存通过现有解码器和CRC校验的完整帧。
    invalid_frames = []  # 保存找到帧头但未通过字段或CRC校验的候选帧。
    outside_chunks = []  # 保存帧与帧之间无法归入协议帧的字节。
    incomplete_tail = b""  # 保存日志末尾未收全的候选帧。
    cursor = 0  # 当前扫描位置。
    header = bytes(HEADER)

    while cursor < len(data):
        frame_start = data.find(header, cursor)
        if frame_start < 0:
            if cursor < len(data):
                outside_chunks.append(data[cursor:])
            break
        if frame_start > cursor:
            outside_chunks.append(data[cursor:frame_start])
        if frame_start + 4 > len(data):
            incomplete_tail = data[frame_start:]
            break

        frame_type = data[frame_start + 3]  # 根据帧类型查询项目定义的固定帧长。
        frame_length = FRAME_LENGTHS.get(frame_type)
        if frame_length is None:
            invalid_frames.append((frame_type, "未知帧类型，跳过一个字节重新同步"))
            cursor = frame_start + 1
            continue

        frame_end = frame_start + frame_length
        if frame_end > len(data):
            next_header = data.find(header, frame_start + len(header))
            if next_header < 0:
                incomplete_tail = data[frame_start:]
                break
            received_length = next_header - frame_start
            invalid_frames.append((frame_type, f"候选帧不完整，收到{received_length}/{frame_length}字节"))
            outside_chunks.append(data[frame_start:next_header])
            incomplete_tail = b""
            cursor = next_header
            continue

        candidate = data[frame_start:frame_end]
        try:
            report = decode_frame(candidate)
        except (ValueError, struct.error) as error:
            invalid_frames.append((frame_type, str(error)))
            cursor = frame_start + 1
            continue

        valid_frames.append((candidate, report))
        cursor = frame_end

    return valid_frames, invalid_frames, outside_chunks, incomplete_tail


def format_channel_flags(channel):
    """@brief 将通道状态整理成简短中文说明。
    @param channel 已解码的通道信息。
    @return 可直接显示的状态文字；无特殊状态时返回空字符串。
    """
    notes = []  # 收集状态位对应的提示。
    if channel.get("fault"):
        notes.append("故障")
    if channel.get("blind"):
        notes.append("盲锁中")
    if channel.get("minute_incomplete"):
        notes.append("本分钟统计不完整")
    if channel.get("detection_ready") is False and not channel.get("fault") and not channel.get("blind"):
        notes.append("当前检测不可用")
    return "、".join(notes)


def summarize_file(file_path):
    """@brief 解析单个日志并输出逐帧及火焰事件统计摘要。
    @param file_path 待读取的日志文件路径。
    @return 无效帧数量；文件读取失败时由调用方处理异常。
    """
    data, extracted_count = read_hex_file(file_path)
    valid_frames, invalid_frames, outside_chunks, incomplete_tail = split_frames(data)
    type_counts = defaultdict(int)  # 按消息类型统计有效帧数。
    minute_max = defaultdict(int)  # 保存每个窗口内出现过的最大设备累计次数。
    legacy_event = False  # 历史01/02帧是否报告过锁存事件。
    legacy_unknown = False  # 历史帧是否明确表示暂时无法判断。
    incomplete_statistics = False  # 分钟窗口内是否出现统计不完整标志。
    current_event = False  # 当前06/07分钟帧是否出现非零事件次数。

    print(f"\n文件：{file_path}")
    print(f"提取字节：{extracted_count}；识别到有效帧：{len(valid_frames)}；无效候选帧：{len(invalid_frames)}")

    for frame, report in valid_frames:
        frame_type = report["type"]
        type_counts[frame_type] += 1
        uptime_ms = report.get("uptime_ms", 0)
        sequence = report.get("sequence", 0)
        prefix = f"类型{frame_type:02X} {FRAME_TYPE_NAMES.get(frame_type, '未知')} 序号{sequence} 开机{uptime_ms}ms"

        if frame_type in (6, 7):
            minute_index = report["minute_index"]
            minute_elapsed = report["minute_elapsed_ms"]
            print(f"  {prefix}：第{minute_index + 1}分钟，已过{minute_elapsed // 1000}秒")
            for channel in report["channels"]:
                count = channel["event_count"]
                minute_max[(report["address"], minute_index, channel["channel"])] = max(
                    minute_max[(report["address"], minute_index, channel["channel"])], count
                )
                if channel["minute_incomplete"]:
                    incomplete_statistics = True
                if count > 0:
                    current_event = True
                flags = format_channel_flags(channel)
                suffix = f"（{flags}）" if flags else ""
                print(f"    {channel['channel']}：{count}次{suffix}")
        elif frame_type in (1, 2):
            decision = report["decision"]
            if decision == 1:
                legacy_event = True
            elif decision == 2:
                legacy_unknown = True
            print(f"  {prefix}：{report['meaning']}")
            for channel in report["channels"]:
                print(f"    {channel['channel']}：{channel['meaning']}")
        elif frame_type == 4:
            print(f"  {prefix}：启动重试上限{report['retry_limit']}次，间隔{report['retry_interval_ms']}ms")
            for channel in report["channels"]:
                print(f"    {channel['channel']}：{channel['meaning']}，已重试{channel['retries_done']}次，结果码{channel['config_result']}")
        elif frame_type == 5:
            print(f"  {prefix}：{report['meaning']}，关联清除请求序号{report['request_sequence']}")
            for channel in report["channels"]:
                print(f"    {channel['channel']}：{channel['meaning']}，结果码{channel['result']}")

    print("有效帧类型统计：" + ("，".join(f"{kind:02X}={count}帧" for kind, count in sorted(type_counts.items())) or "无"))
    if current_event or legacy_event:
        print("事件判断：检测到有效报告中记录的火焰事件。")
    elif legacy_unknown or incomplete_statistics:
        print("事件判断：没有有效帧确认事件，但历史帧无法判断或本分钟统计不完整，不能排除发生过事件。")
    elif any(frame[1]["type"] in (1, 2, 6, 7) for frame in valid_frames):
        print("事件判断：有效检测/计数帧未报告火焰事件。")
    else:
        print("事件判断：数据中没有有效检测/计数帧，无法判断是否发生火焰事件。")

    if minute_max:
        totals = {channel_name: 0 for channel_name in CHANNEL_NAMES}
        for (_, _, channel_name), maximum in minute_max.items():
            totals[channel_name] += maximum
        print("分钟窗口累计次数（同一窗口重复帧只取最大值；日志可能未覆盖整分钟）：")
        print("  " + "，".join(f"{channel}={count}次" for channel, count in totals.items()))

    if invalid_frames:
        print("无效候选帧：")
        for frame_type, reason in invalid_frames[:MAX_REPORTED_FRAGMENTS]:
            print(f"  类型{frame_type:02X}：{reason}")
        if len(invalid_frames) > MAX_REPORTED_FRAGMENTS:
            print(f"  另有{len(invalid_frames) - MAX_REPORTED_FRAGMENTS}个未展开")
    outside_count = sum(len(chunk) for chunk in outside_chunks)
    if outside_count:
        print(f"帧外字节：{outside_count}个（可能是日志文本、噪声或帧间残留）")
    if incomplete_tail:
        print(f"末尾未收完整字节：{len(incomplete_tail)}个；片段：{' '.join(f'{byte:02X}' for byte in incomplete_tail[:32])}")
    print("说明：事件次数是探头通道观测到的事件数，不等于独立物理火焰数量；重复快照不会重复累计。")
    return len(invalid_frames)


def main():
    """@brief 解析拖放到本程序的一个或多个文本日志文件。
    @return 成功时返回零；文件无法读取时返回非零。
    """
    file_paths = [Path(argument) for argument in sys.argv[1:]]  # 命令行传入的拖放文件列表。
    if not file_paths:
        print("用法：将含十六进制RS485数据的TXT或LOG文件拖到‘解析RS485日志.bat’上。")
        print("支持混合帧、连帧和分行十六进制字节；解析使用项目当前接收器的字段与CRC规则。")
        return 0

    read_failures = 0  # 读取失败的文件数量。
    for file_path in file_paths:
        try:
            summarize_file(file_path)
        except (OSError, UnicodeError) as error:
            read_failures += 1
            print(f"\n文件读取失败：{file_path}\n原因：{error}")
    return 1 if read_failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
