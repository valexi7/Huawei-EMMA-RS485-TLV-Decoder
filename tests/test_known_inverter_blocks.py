"""Regression checks for inverter register windows confirmed by the July 2026 portal snapshot."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DECODER = ROOT / "components" / "huawei_emma_tlv" / "huawei_proprietary.inc"
PACKAGE = ROOT / "huawei-emma-rs485.yaml"


def read_field(block_start: int, word_count: int, payload: bytes, register: int, words: int, *, signed=False):
    offset_words = register - block_start
    if offset_words < 0 or offset_words + words > word_count:
        return None
    start = offset_words * 2
    end = start + words * 2
    if end > len(payload):
        return None
    return int.from_bytes(payload[start:end], "big", signed=signed)


def block_with_fields(block_start: int, word_count: int, fields: dict[int, bytes]) -> bytes:
    payload = bytearray(word_count * 2)
    for register, value in fields.items():
        offset = (register - block_start) * 2
        payload[offset : offset + len(value)] = value
    return bytes(payload)


class KnownInverterBlockTests(unittest.TestCase):
    def test_realtime_window_matches_portal_values(self):
        payload = block_with_fields(
            0x7D40,
            33,
            {
                0x7D40: (2198).to_bytes(4, "big", signed=True),
                0x7D45: (2385).to_bytes(2, "big"),
                0x7D46: (2393).to_bytes(2, "big"),
                0x7D47: (2389).to_bytes(2, "big"),
                0x7D48: (1553).to_bytes(4, "big", signed=True),
                0x7D4A: (1550).to_bytes(4, "big", signed=True),
                0x7D4C: (1550).to_bytes(4, "big", signed=True),
                0x7D50: (1089).to_bytes(4, "big", signed=True),
                0x7D52: (-2).to_bytes(4, "big", signed=True),
                0x7D54: (1000).to_bytes(2, "big", signed=True),
                0x7D55: (4995).to_bytes(2, "big"),
                0x7D57: (379).to_bytes(2, "big", signed=True),
                0x7D58: (3000).to_bytes(2, "big"),
                0x7D59: (0x0200).to_bytes(2, "big"),
                0x7D5B: (1784186939).to_bytes(4, "big"),
                0x7D5D: (0xFFFFFFFF).to_bytes(4, "big"),
            },
        )
        self.assertEqual(read_field(0x7D40, 33, payload, 0x7D40, 2, signed=True), 2198)
        self.assertEqual(read_field(0x7D40, 33, payload, 0x7D45, 1) * 0.1, 238.5)
        self.assertEqual(read_field(0x7D40, 33, payload, 0x7D48, 2, signed=True) * 0.001, 1.553)
        self.assertEqual(read_field(0x7D40, 33, payload, 0x7D50, 2, signed=True), 1089)
        self.assertEqual(read_field(0x7D40, 33, payload, 0x7D52, 2, signed=True), -2)
        self.assertEqual(read_field(0x7D40, 33, payload, 0x7D54, 1, signed=True) * 0.001, 1.0)
        self.assertEqual(read_field(0x7D40, 33, payload, 0x7D55, 1) * 0.01, 49.95)
        self.assertEqual(read_field(0x7D40, 33, payload, 0x7D57, 1, signed=True) * 0.1, 37.9)
        self.assertEqual(read_field(0x7D40, 33, payload, 0x7D58, 1) * 0.001, 3.0)
        self.assertEqual(read_field(0x7D40, 33, payload, 0x7D59, 1), 0x0200)
        self.assertEqual(read_field(0x7D40, 33, payload, 0x7D5B, 2), 1784186939)
        self.assertEqual(read_field(0x7D40, 33, payload, 0x7D5D, 2), 0xFFFFFFFF)

    def test_ac_and_dc_daily_energy_use_distinct_registers(self):
        payload = block_with_fields(
            0x7D6E,
            10,
            {
                0x7D72: (117).to_bytes(4, "big"),
                0x7D80: (159).to_bytes(4, "big"),
            },
        )
        self.assertEqual(read_field(0x7D6E, 10, payload, 0x7D72, 2) * 0.01, 1.17)
        self.assertIsNone(read_field(0x7D6E, 10, payload, 0x7D80, 2))

        daily_dc_payload = block_with_fields(0x7D80, 2, {0x7D80: (1637).to_bytes(4, "big")})
        self.assertEqual(read_field(0x7D80, 2, daily_dc_payload, 0x7D80, 2) * 0.01, 16.37)

        mppt1_payload = block_with_fields(0x7DD4, 2, {0x7DD4: (223205).to_bytes(4, "big")})
        self.assertEqual(read_field(0x7DD4, 2, mppt1_payload, 0x7DD4, 2) * 0.01, 2232.05)

    def test_alarm_window_and_fault_code(self):
        alarm_payload = block_with_fields(
            0x7D08,
            5,
            {
                0x7D08: (0x0080).to_bytes(2, "big"),
                0x7D09: (0x0400).to_bytes(2, "big"),
                0x7D0A: (0x0020).to_bytes(2, "big"),
            },
        )
        self.assertEqual(read_field(0x7D08, 5, alarm_payload, 0x7D08, 1), 0x0080)
        self.assertEqual(read_field(0x7D08, 5, alarm_payload, 0x7D09, 1), 0x0400)
        self.assertEqual(read_field(0x7D08, 5, alarm_payload, 0x7D0A, 1), 0x0020)

        realtime_payload = block_with_fields(0x7D40, 33, {0x7D5A: (17).to_bytes(2, "big")})
        self.assertEqual(read_field(0x7D40, 33, realtime_payload, 0x7D5A, 1), 17)

    def test_rated_power_is_inside_capabilities_window(self):
        payload = block_with_fields(0x7574, 13, {0x7579: (10000).to_bytes(4, "big")})
        self.assertEqual(read_field(0x7574, 13, payload, 0x7579, 2), 10000)

    def test_inverter_meter_mirror_matches_confirmed_values(self):
        meter_payload = block_with_fields(
            0x90F3,
            32,
            {
                0x90F3: (-59).to_bytes(4, "big", signed=True),
                0x90F5: (-99).to_bytes(4, "big", signed=True),
                0x90F7: (90).to_bytes(4, "big", signed=True),
                0x90F9: (31).to_bytes(4, "big", signed=True),
                0x90FB: (394).to_bytes(4, "big", signed=True),
                0x90FD: (-32).to_bytes(2, "big", signed=True),
                0x910C: (-90).to_bytes(4, "big", signed=True),
                0x910E: (-23).to_bytes(4, "big", signed=True),
                0x9110: (157).to_bytes(4, "big", signed=True),
            },
        )
        self.assertEqual(read_field(0x90F3, 32, meter_payload, 0x90F3, 2, signed=True) * 0.01, -0.59)
        self.assertEqual(read_field(0x90F3, 32, meter_payload, 0x90F5, 2, signed=True) * 0.01, -0.99)
        self.assertEqual(read_field(0x90F3, 32, meter_payload, 0x90F7, 2, signed=True) * 0.01, 0.9)
        self.assertEqual(read_field(0x90F3, 32, meter_payload, 0x90F9, 2, signed=True), 31)
        self.assertEqual(read_field(0x90F3, 32, meter_payload, 0x90FB, 2, signed=True), 394)
        self.assertEqual(read_field(0x90F3, 32, meter_payload, 0x90FD, 1, signed=True) * 0.001, -0.032)
        self.assertEqual(read_field(0x90F3, 32, meter_payload, 0x910C, 2, signed=True), -90)
        self.assertEqual(read_field(0x90F3, 32, meter_payload, 0x910E, 2, signed=True), -23)
        self.assertEqual(read_field(0x90F3, 32, meter_payload, 0x9110, 2, signed=True), 157)

    def test_decoder_and_entities_match_confirmed_map(self):
        source = DECODER.read_text(encoding="utf-8")
        package = PACKAGE.read_text(encoding="utf-8")
        expected = {
            "HP_TAG_DAILY_ENERGY": "0x7D72",
            "HP_TAG_DAILY_DC_ENERGY_YIELD": "0x7D80",
            "HP_TAG_MPPT1_TOTAL_DC_ENERGY_YIELD": "0x7DD4",
            "HP_REG_STARTUP_TIME": "0x7D5B",
            "HP_REG_SHUTDOWN_TIME": "0x7D5D",
            "HP_REG_RATED_POWER": "0x7579",
            "HP_REG_INVERTER_FAULT_CODE": "0x7D5A",
            "HP_REG_INVERTER_ALARM_1": "0x7D08",
            "HP_REG_INVERTER_ALARM_2": "0x7D09",
            "HP_REG_INVERTER_ALARM_3": "0x7D0A",
            "HP_REG_METER_PHASE_A_CURRENT": "0x90F3",
            "HP_REG_METER_PHASE_B_CURRENT": "0x90F5",
            "HP_REG_METER_PHASE_C_CURRENT": "0x90F7",
            "HP_REG_METER_ACTIVE_POWER": "0x90F9",
            "HP_REG_METER_REACTIVE_POWER": "0x90FB",
            "HP_REG_METER_POWER_FACTOR": "0x90FD",
            "HP_REG_METER_PHASE_A_ACTIVE_POWER": "0x910C",
            "HP_REG_METER_PHASE_B_ACTIVE_POWER": "0x910E",
            "HP_REG_METER_PHASE_C_ACTIVE_POWER": "0x9110",
        }
        for name, value in expected.items():
            self.assertRegex(source, rf"{name}\s*=\s*{re.escape(value)}\s*;")
        for entity_id in (
            "inverter_pv1_voltage",
            "inverter_pv1_current",
            "inverter_input_power",
            "inverter_rated_power",
            "inverter_grid_frequency",
            "inverter_internal_temperature",
            "inverter_insulation_resistance",
            "inverter_status",
            "inverter_active_alarms",
            "inverter_fault_code",
            "inverter_alarm_1_raw",
            "inverter_alarm_2_raw",
            "inverter_alarm_3_raw",
            "inverter_daily_dc_energy_yield",
            "inverter_mppt1_total_dc_energy_yield",
            "meter_phase_a_current",
            "meter_phase_b_current",
            "meter_phase_c_current",
            "meter_active_power",
            "meter_reactive_power",
            "meter_power_factor",
            "meter_phase_a_power",
            "meter_phase_b_power",
            "meter_phase_c_power",
        ):
            self.assertIn(f"id: {entity_id}", package)
        insulation_entity = re.search(
            r"id: inverter_insulation_resistance.*?(?=\n\s+- platform:|\Z)",
            package,
            re.DOTALL,
        )
        self.assertIsNotNone(insulation_entity)
        self.assertIn("entity_category: diagnostic", insulation_entity.group(0))
        self.assertIn('"ZZ Reverse Engineering - %s FC41 Tags %u"', source)
        self.assertIn('"ZZ Reverse Engineering - Unknown FC41 Device Tags %u"', source)
        self.assertNotIn("id: inverter_phase_a_power", package)


if __name__ == "__main__":
    unittest.main()
