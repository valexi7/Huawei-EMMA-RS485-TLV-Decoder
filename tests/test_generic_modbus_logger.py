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

    def test_manual_tou_probe_reads_expected_registers_in_order(self):
        source = DECODER.read_text(encoding="utf-8")
        package = PACKAGE.read_text(encoding="utf-8")

        self.assertIn("huawei_modbus_send_read_request", source)
        self.assertIn("HP_REG_BATTERY_TOU_EXCESS_PV = 0xB8C3", source)

        sequence = source.index("static inline void hp_modbus_advance_tou_read_sequence")
        tou = source.index("WAIT_TOU_PERIODS", sequence)
        mode = source.index("HP_REG_BATTERY_WORKING_MODE_SETTING", tou)
        excess_pv = source.index("HP_REG_BATTERY_TOU_EXCESS_PV", mode)
        self.assertLess(tou, mode)
        self.assertLess(mode, excess_pv)
        self.assertIn("huawei_modbus_start_tou_read_sequence", package)
        self.assertIn("wait_until:", package)
        self.assertIn("huawei_modbus_diagnostic_response_timeout", package)
        self.assertNotIn("huawei_modbus_diagnostic_read_delay", package)
        self.assertIn('name: "ZZ Reverse Engineering - Read TOU Configuration"', package)
        self.assertIn("disabled_by_default: true", package)

    def test_response_driven_probe_resolves_dual_valid_read_shape(self):
        source = DECODER.read_text(encoding="utf-8")
        response = bytes.fromhex("0C 03 02 00 01 54 45")
        ambiguous = response + b"\x00"

        self.assertEqual(modbus_crc(response[:-2]), int.from_bytes(response[-2:], "little"))
        self.assertEqual(modbus_crc(ambiguous[:-2]), int.from_bytes(ambiguous[-2:], "little"))
        self.assertIn("diagnostic_response || expecting_response || !request_valid", source)
        self.assertIn("hp_modbus_tou_response_matches", source)


if __name__ == "__main__":
    unittest.main()
