from __future__ import annotations

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from analyze_huawei_csv import (  # noqa: E402
    Block,
    extract_raw_frame,
    modbus_crc,
    parse_current_data_frame,
    read_register,
    timestamp_second,
)


class CsvAnalyzerTests(unittest.TestCase):
    def test_extracts_only_the_final_raw_frame(self):
        value = "{summary=tag=0x7D50 u32=947, raw=0C 41 35 02 D7 00 6A B4}"
        self.assertEqual(extract_raw_frame(value), bytes.fromhex("0C 41 35 02 D7 00 6A B4"))

    def test_parses_finnish_and_colon_timestamps(self):
        self.assertEqual(timestamp_second("15.7.2026 klo 20.59.16"), 20 * 3600 + 59 * 60 + 16)
        self.assertEqual(timestamp_second("20:59:16"), 20 * 3600 + 59 * 60 + 16)

    def test_replays_a_bounded_current_data_block(self):
        payload = bytes.fromhex("D7 01 90 CA 04 00 02 36 6C 00 02 2C EA")
        body = bytes([0x0C, 0x41, 0x35, len(payload)]) + payload
        frame = body + modbus_crc(body).to_bytes(2, "little")
        raw_count, flagged, blocks = parse_current_data_frame(frame)
        self.assertEqual(raw_count, 1)
        self.assertFalse(flagged)
        self.assertEqual(read_register(blocks[0], 0x90CA, 2), 145004)
        self.assertEqual(read_register(blocks[0], 0x90CC, 2), 142570)
        self.assertIsNone(read_register(blocks[0], 0x90CE, 2))

    def test_rejects_a_block_crossing_the_payload_boundary(self):
        payload = bytes.fromhex("D7 01 90 CA 04 00 02")
        body = bytes([0x0C, 0x41, 0x35, len(payload)]) + payload
        frame = body + modbus_crc(body).to_bytes(2, "little")
        with self.assertRaisesRegex(ValueError, "crosses the payload boundary"):
            parse_current_data_frame(frame)


if __name__ == "__main__":
    unittest.main()
