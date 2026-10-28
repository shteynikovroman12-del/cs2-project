from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from analyze_client import PE64, verify_report


def fixture():
    data = bytearray(0x1000)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', data, 0x84, 0x8664, 1)
    struct.pack_into('<H', data, 0x94, 0xf0)
    struct.pack_into('<H', data, 0x98, 0x20b)
    struct.pack_into('<Q', data, 0xb0, 0x180000000)
    struct.pack_into('<I', data, 0xd0, 0x3000)
    data[0x188:0x190] = b'.data\0\0\0'
    struct.pack_into('<IIII', data, 0x190, 0x1800, 0x1000, 0xe00, 0x200)
    data[0x280:0x28a] = b'TestClass\0'
    data[0x2a0:0x2a7] = b'm_Test\0'
    struct.pack_into('<Q', data, 0x308, 0x180001080)
    struct.pack_into('<IH', data, 0x320, 0x40, 1)
    struct.pack_into('<Q', data, 0x330, 0x180001200)
    struct.pack_into('<Q', data, 0x400, 0x1800010a0)
    struct.pack_into('<i', data, 0x410, 0x18)
    return data


class ClientAnalysisTests(unittest.TestCase):
    def test_mapping_and_complete_record(self):
        pe = PE64(fixture())
        self.assertEqual(pe.va_to_offset(0x180001200), 0x400)
        self.assertEqual(pe.offset_to_va(0x400), 0x180001200)
        record = pe.schema('TestClass')
        self.assertEqual(record, dict(record_rva=0x1100, size=0x40, fields={'m_Test': 0x18}))
        # Zero-filled image memory is not file-backed input.
        with self.assertRaises(ValueError):
            pe.va_to_offset(0x180002000)

    def test_truncated_or_wrong_architecture(self):
        for length in (0, 2, 0x3f, 0x85, 0x19f, 0xfff):
            with self.subTest(length=length), self.assertRaises(ValueError):
                PE64(fixture()[:length])
        data = fixture()
        struct.pack_into('<H', data, 0x84, 0x14c)
        with self.assertRaises(ValueError):
            PE64(data)

    def test_rejects_old_layout_and_corrupt_fields(self):
        for offset, fmt, value in ((0x320, '<I', 0), (0x324, '<H', 2049),
                                   (0x330, '<Q', 0xffffffffffffffff),
                                   (0x400, '<Q', 0), (0x410, '<i', -1), (0x410, '<i', 0x40)):
            with self.subTest(offset=offset, value=value):
                data = fixture()
                struct.pack_into(fmt, data, offset, value)
                with self.assertRaises(ValueError):
                    PE64(data).schema('TestClass')
        data = fixture()
        struct.pack_into('<IH', data, 0x318, 0x40, 1)
        struct.pack_into('<IH', data, 0x320, 0, 0)
        struct.pack_into('<Q', data, 0x328, 0x180001200)
        with self.assertRaises(ValueError):
            PE64(data).schema('TestClass')

    def test_rejects_ambiguous_record(self):
        data = fixture()
        data[0x500:0x538] = data[0x300:0x338]
        with self.assertRaises(ValueError):
            PE64(data).schema('TestClass')
        with self.assertRaises(ValueError):
            PE64(fixture()).schema('MissingClass')

    def test_report_mismatch(self):
        module = dict(file='client.dll', bytes=100, sha256='a' * 64)
        entry = dict(file='game/csgo/bin/win64/client.dll', present=True, bytes=100, sha256='a' * 64)
        report = dict(report_format=1, steam_app_id=730, steam_build_id='test', files=[entry])
        self.assertEqual(verify_report([module], report), 'test')
        entry['sha256'] = 'b' * 64
        with self.assertRaises(ValueError):
            verify_report([module], report)
        report['files'].append(dict(entry))
        with self.assertRaises(ValueError):
            verify_report([module], report)

    def test_cli_preserves_input_and_existing_output_on_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dll = root / 'client.dll'
            dll.write_bytes(fixture())
            command = [sys.executable, str(Path(__file__).resolve().parents[1] / 'tools/analyze_client.py'),
                       '--client', str(dll), '--class-name', 'TestClass']
            result = subprocess.run(command + ['--output', str(dll)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(dll.read_bytes(), fixture())
            report = root / 'report.json'
            report.write_text('{}')
            output = root / 'analysis.json'
            output.write_text('previous result')
            result = subprocess.run(command + ['--output', str(output), '--client-report', str(report)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_text(), 'previous result')
if __name__ == '__main__':
    unittest.main()
