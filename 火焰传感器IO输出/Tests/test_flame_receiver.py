"""接收端流式解析、独立CRC及固件编码测试向量验证；不访问实物。"""
import importlib.util
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('flame_receiver', ROOT / 'tools/flame_receiver.py')
receiver_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(receiver_module)
VECTOR = Path(sys.argv.pop(1))


def independent_crc(data):
    # 左移0x8005实现，与固件右移算法独立交叉核对。
    crc = 0xFFFF
    for byte in data:
        reflected = int(f'{byte:08b}'[::-1], 2)
        crc ^= reflected << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x8005 if crc & 0x8000 else 0)) & 0xFFFF
    return int(f'{crc:016b}'[::-1], 2)


def frame(sequence=1, uptime=1000, address=1):
    data = bytearray(VECTOR.read_bytes())
    data[4] = address
    struct.pack_into('<H', data, 5, sequence)
    struct.pack_into('<I', data, 9, uptime)
    struct.pack_into('<H', data, 63, independent_crc(data[:63]))
    return bytes(data)


class ReceiverTests(unittest.TestCase):
    def test_firmware_vector(self):
        data = VECTOR.read_bytes()
        self.assertEqual(len(data), 65)
        self.assertEqual(independent_crc(data[:63]), struct.unpack_from('<H', data, 63)[0])
        self.assertEqual(receiver_module.crc16(b'123456789'), 0x4B37)
        decoded = receiver_module.decode_frame(data)
        self.assertEqual(decoded['sequence'], 0x1234)
        self.assertEqual(decoded['uptime_ms'], 0x01020304)
        self.assertEqual(decoded['decision'], 1)
        self.assertEqual(decoded['channels'][1]['result'], -4)
        self.assertEqual(decoded['channels'][1]['adc_raw'], 0x1234)
        self.assertIsNone(decoded['channels'][0]['adc_raw'])

    def test_every_split_and_coalesced_frames(self):
        for split in range(1, 65):
            rx = receiver_module.FlameReceiver()
            self.assertEqual(rx.feed(frame()[:split], 0), [])
            self.assertEqual(len(rx.feed(frame()[split:], 100)), 1)
        rx = receiver_module.FlameReceiver()
        self.assertEqual(len(rx.feed(b'noise' + frame(1) + frame(2) + frame(3), 0)), 3)
        self.assertTrue(rx.status(2999)['online'])
        self.assertFalse(rx.status(3000)['online'])
        self.assertEqual(rx.status(3000)['effective_decision'], 2)
        self.assertEqual(rx.status(3000)['reported_decision'], 1)

    def test_crc_and_resynchronization(self):
        corrupt = bytearray(frame())
        corrupt[23] ^= 1
        rx = receiver_module.FlameReceiver()
        self.assertEqual(len(rx.feed(b'\xAA' + corrupt + frame(), 0)), 1)
        self.assertEqual(rx.feed(corrupt, 1000), [])
        self.assertFalse(rx.status(3000)['online'])  # 损坏帧不刷新在线时间。
        for offset in (0, 2, 3, 7, 13, 14, 15):
            bad = bytearray(frame())
            bad[offset] = 255
            struct.pack_into('<H', bad, 63, independent_crc(bad[:63]))
            with self.assertRaises(ValueError):
                receiver_module.decode_frame(bad)

    def test_partial_timeout(self):
        rx = receiver_module.FlameReceiver()
        rx.feed(frame()[:30], 0)
        self.assertEqual(rx.feed(frame()[30:], 200), [])
        self.assertFalse(rx.status(200)['online'])
        self.assertEqual(len(rx.feed(frame(), 210)), 1)
        rx = receiver_module.FlameReceiver()
        rx.feed(b'\xAA', 0)
        self.assertEqual(len(rx.feed(frame()[1:], 1)), 1)

    def test_loss_restart_wrap_address(self):
        rx = receiver_module.FlameReceiver()
        rx.feed(frame(65535, 0xFFFFFF00), 0)
        report = rx.feed(frame(0, 20), 100)[0]
        self.assertFalse(report['restarted'])
        self.assertEqual(report['lost_frames'], 0)
        self.assertEqual(rx.feed(frame(3, 200), 200)[0]['lost_frames'], 2)
        self.assertTrue(rx.feed(frame(3, 300), 300)[0]['duplicate'])
        self.assertTrue(rx.feed(frame(0, 1), 400)[0]['restarted'])
        self.assertEqual(rx.feed(frame(1, 100, 2), 1000), [])
        self.assertFalse(rx.status(3400)['online'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
