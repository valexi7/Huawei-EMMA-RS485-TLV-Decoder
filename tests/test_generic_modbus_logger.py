"""Regression checks for the passive standard Modbus RTU logger."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DECODER = ROOT / "components" / "huawei_emma_tlv" / "huawei_proprietary.inc"
PACKAGE = ROOT / "huawei-emma-rs485.yaml"


def modbus_crc(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def frame(body: bytes) -> bytes:
    return body + modbus_crc(body).to_bytes(2, "little")


class GenericModbusLoggerTests(unittest.TestCase):
    def test_representative_frames_have_expected_rtu_shapes(self):
        read_request = frame(bytes.fromhex("0C 03 90 A1 00 01"))
        read_response = frame(bytes.fromhex("0C 03 02 02 34"))
        write_single = frame(bytes.fromhex("0C 06 B7 EE 00 05"))
        write_multiple = frame(bytes.fromhex("0C 10 B8 97 00 02 04 00 01 00 02"))

        self.assertEqual(len(read_request), 8)
        self.assertEqual(len(read_response), 7)
        self.assertEqual(len(write_single), 8)
        self.assertEqual(len(write_multiple), 13)
        for item in (read_request, read_response, write_single, write_multiple):
            self.assertEqual(modbus_crc(item[:-2]), int.from_bytes(item[-2:], "little"))

    def test_package_calls_both_passive_decoders(self):
        source = DECODER.read_text(encoding="utf-8")
        package = PACKAGE.read_text(encoding="utf-8")

        self.assertIn("huawei_decode_generic_modbus_uart_buffer", source)
        self.assertIn("hp_modbus_crc_matches", source)
        self.assertIn('"CONTROL RTU dev=%u', source)
        self.assertIn("remaining_charge_discharge_time=%umin", source)
        self.assertIn("huawei_decode_proprietary_uart_buffer(bytes);", package)
        self.assertIn("huawei_decode_generic_modbus_uart_buffer(bytes);", package)
        self.assertIn("huawei_modbus: ${huawei_log_level}", package)


if __name__ == "__main__":
    unittest.main()
