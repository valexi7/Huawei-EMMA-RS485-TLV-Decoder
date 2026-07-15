"""Regression checks for battery register blocks observed in the July 2026 capture."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DECODER = ROOT / "components" / "huawei_emma_tlv" / "huawei_proprietary.inc"


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


class KnownBatteryBlockTests(unittest.TestCase):
    def test_total_energy_block_uses_register_boundaries(self):
        # Captured 0x90CA block: 1450.04 kWh, 1425.70 kWh, 20.700 kWh.
        payload = bytes.fromhex("00 02 36 6C 00 02 2C EA 00 00 50 DC 00 03 00 01 00 00 00 00")
        self.assertEqual(read_field(0x90CA, 10, payload, 0x90CA, 2) * 0.01, 1450.04)
        self.assertEqual(read_field(0x90CA, 10, payload, 0x90CC, 2) * 0.01, 1425.70)
        self.assertEqual(read_field(0x90CA, 10, payload, 0x90CE, 2) * 0.001, 20.700)

    def test_field_must_be_fully_covered_and_present(self):
        self.assertIsNone(read_field(0x90CA, 3, bytes(6), 0x90CC, 2))
        self.assertIsNone(read_field(0x90CC, 2, bytes(3), 0x90CC, 2))
        self.assertIsNone(read_field(0x90CD, 4, bytes(8), 0x90CC, 2))

    def test_global_values_inside_larger_blocks(self):
        payload = block_with_fields(
            0x9088,
            24,
            {
                0x9089: (-2239).to_bytes(4, "big", signed=True),
                0x908B: (7456).to_bytes(2, "big"),
                0x908C: (320).to_bytes(2, "big"),
                0x9097: (632).to_bytes(4, "big"),
                0x9099: (665).to_bytes(4, "big"),
                0x909D: (-33).to_bytes(2, "big", signed=True),
                0x909E: (413).to_bytes(2, "big"),
            },
        )
        self.assertEqual(read_field(0x9088, 24, payload, 0x9089, 2, signed=True), -2239)
        self.assertEqual(read_field(0x9088, 24, payload, 0x908B, 1) * 0.1, 745.6)
        self.assertEqual(read_field(0x9088, 24, payload, 0x908C, 1) * 0.1, 32.0)
        self.assertEqual(read_field(0x9088, 24, payload, 0x9097, 2) * 0.01, 6.32)
        self.assertEqual(read_field(0x9088, 24, payload, 0x9099, 2) * 0.01, 6.65)
        self.assertAlmostEqual(read_field(0x9088, 24, payload, 0x909D, 1, signed=True) * 0.1, -3.3)
        self.assertAlmostEqual(read_field(0x9088, 24, payload, 0x909E, 1, signed=True) * 0.1, 41.3)

    def test_pack_2_identity_is_at_byte_18_and_firmware_at_byte_38(self):
        payload = block_with_fields(
            0x9559,
            40,
            {
                0x9562: b"EX2580025278",
                0x956C: b"V200R025C00SPC102",
                0x957F: (310).to_bytes(2, "big"),
            },
        )
        self.assertEqual(payload[18:30], b"EX2580025278")
        self.assertEqual(payload[38:55], b"V200R025C00SPC102")
        self.assertEqual(read_field(0x9559, 40, payload, 0x957F, 1) * 0.1, 31.0)

    def test_decoder_constants_match_capture_map(self):
        source = DECODER.read_text(encoding="utf-8")
        expected = {
            "HP_TAG_BATTERY_DISCHARGED_TODAY": "0x9099",
            "HP_TAG_BATTERY_TOTAL_BLOCK": "0x90CA",
            "HP_TAG_BATTERY_TOTAL_DISCHARGE": "0x90CC",
            "HP_TAG_BATTERY_RATED_CAPACITY": "0x90CE",
            "HP_TAG_BATTERY_PACK_3_TOTAL_ENERGY": "0x95B4",
        }
        for name, value in expected.items():
            self.assertRegex(source, rf"{name}\s*=\s*{re.escape(value)}\s*;")
        self.assertIn("type >= 0x20 && !hp_tlv_inline_length_tag(tag)", source)


if __name__ == "__main__":
    unittest.main()
