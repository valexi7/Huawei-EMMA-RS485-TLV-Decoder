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

    def test_captured_tou_write_request_has_valid_shape_and_crc(self):
        words = [1, 20 * 60, 8 * 60, 0x01FF] + [0] * 39
        payload = b"".join(word.to_bytes(2, "big") for word in words)
        request = frame(bytes.fromhex("0C 10 B8 97 00 2B 56") + payload)

        self.assertEqual(len(words), 43)
        self.assertEqual(len(payload), 86)
        self.assertEqual(len(request), 95)
        self.assertEqual(request[-2:], bytes.fromhex("02 43"))

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
        self.assertIn('huawei_modbus_tou_device_id: "0"', package)
        self.assertIn("${huawei_modbus_tou_device_id}", package)
        self.assertNotIn("huawei_modbus_inverter_device_id", package)

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
        self.assertNotIn("already available from FC41/readback; no manual read sent", source)
        self.assertIn('name: "Battery TOU Read Configuration"', package)
        self.assertNotIn('name: "ZZ Reverse Engineering - Read TOU Configuration"', package)
        self.assertIn("disabled_by_default: true", package)
        button = package.index("id: huawei_read_tou_configuration_button")
        self.assertIn("entity_category: config", package[button : button + 250])

    def test_response_driven_probe_resolves_dual_valid_read_shape(self):
        source = DECODER.read_text(encoding="utf-8")
        response = bytes.fromhex("0C 03 02 00 01 54 45")
        ambiguous = response + b"\x00"

        self.assertEqual(modbus_crc(response[:-2]), int.from_bytes(response[-2:], "little"))
        self.assertEqual(modbus_crc(ambiguous[:-2]), int.from_bytes(ambiguous[-2:], "little"))
        self.assertIn("diagnostic_response || expecting_response || !request_valid", source)
        self.assertIn("hp_modbus_tou_response_matches", source)

    def test_tou_editor_is_disabled_and_writes_only_changed_groups(self):
        source = DECODER.read_text(encoding="utf-8")
        package = PACKAGE.read_text(encoding="utf-8")

        for entity_id in (
            "battery_working_mode_control",
            "battery_excess_pv_behavior_control",
            "battery_tou_selected_period",
            "battery_tou_period_action",
            "battery_tou_period_days",
            "battery_tou_period_start",
            "battery_tou_period_end",
            "battery_tou_clear_selected_period_button",
            "battery_tou_write_configuration_button",
        ):
            entity = package.index(f"id: {entity_id}")
            self.assertIn("disabled_by_default: true", package[entity : entity + 300])

        self.assertIn("hp_tou_editor_schedule_changed()", source)
        self.assertIn("hp_tou_editor_excess_pv_changed()", source)
        self.assertIn("hp_tou_editor_working_mode_changed()", source)
        self.assertIn('"Battery TOU configuration is unchanged; no registers written"', source)
        self.assertIn("hp_tou_write.verification_seen == 0x07", source)
        self.assertIn("hp_tou_store_readback(bytes, start + 3, byte_count, first_register);", source)
        self.assertIn("huawei_modbus_poll_tou_write_sequence();", package)
        self.assertIn("WAIT_SCHEDULE_READBACK", source)
        self.assertIn('"Battery TOU %s write confirmed by readback"', source)

        sequence = source.index("static inline void hp_tou_send_next_write")
        schedule = source.index("HP_REG_BATTERY_TOU_PERIODS", sequence)
        excess = source.index("HP_REG_BATTERY_TOU_EXCESS_PV", schedule)
        mode = source.index("HP_REG_BATTERY_WORKING_MODE_SETTING", excess)
        self.assertLess(schedule, excess)
        self.assertLess(excess, mode)

    def test_working_mode_control_options_follow_register_values(self):
        package = PACKAGE.read_text(encoding="utf-8")
        start = package.index("id: battery_working_mode_control")
        end = package.index("id: battery_excess_pv_behavior_control", start)
        control = package[start:end]
        options = [
            "Adaptive",
            "Fixed charge/discharge",
            "Maximum self-consumption",
            "Time of use (LG RESU)",
            "Fully fed to grid",
            "TOU (LUNA2000)",
        ]
        positions = [control.index(f'- "{option}"') for option in options]
        self.assertEqual(positions, sorted(positions))

    def test_fc41_configuration_populates_the_tou_editor(self):
        source = DECODER.read_text(encoding="utf-8")
        package = PACKAGE.read_text(encoding="utf-8")

        self.assertIn("hp_tou_editor_apply_schedule(schedule);", source)
        self.assertIn("hp_tou_editor_apply_working_mode(u16);", source)
        self.assertIn("hp_tou_editor_apply_excess_pv(u16);", source)
        self.assertIn("hp_tou_confirm_schedule_from_fc41(schedule);", source)
        self.assertNotIn("already available from FC41/readback; no manual read sent", source)
        for entity_id in (
            "battery_tou_period_start",
            "battery_tou_period_end",
            "battery_tou_period_action",
            "battery_tou_period_days",
        ):
            self.assertIn(f"id({entity_id}).update();", source)

        sensor = package.index("id: battery_excess_pv_behavior\n")
        self.assertIn('name: "Battery Excess PV Behavior"', package[sensor : sensor + 250])
        self.assertIn("entity_category: diagnostic", package[sensor : sensor + 250])
        self.assertIn('name: "Battery Excess PV Behavior Control"', package)

    def test_raw_modbus_controls_are_disabled_and_guard_empty_writes(self):
        source = DECODER.read_text(encoding="utf-8")
        package = PACKAGE.read_text(encoding="utf-8")

        entities = (
            "modbus_diagnostic_device_id",
            "modbus_diagnostic_function_code",
            "modbus_diagnostic_start_register",
            "modbus_diagnostic_word_count",
            "modbus_diagnostic_write_request",
            "modbus_diagnostic_send_button",
            "modbus_diagnostic_response",
        )
        positions = []
        for entity_id in entities:
            entity = package.index(f"id: {entity_id}")
            positions.append(package.index('name: "ZZ Modbus Test - ', entity))
            self.assertIn("disabled_by_default: true", package[entity : entity + 350])
            self.assertIn("entity_category: diagnostic", package[entity : entity + 350])

        names = [package[position : package.index("\n", position)] for position in positions]
        prefixes = [int(name.split(" - ", 1)[1].split(" ", 1)[0]) for name in names]
        self.assertEqual(prefixes, list(range(1, 8)))
        self.assertIn("initial_value: 12", package)
        self.assertIn("initial_value: 30000", package)
        self.assertIn("initial_value: 15", package)
        self.assertIn('initial_option: "Read Holding Registers (0x03)"', package)
        self.assertIn('initial_value: ""', package)

        self.assertIn("huawei_modbus_send_diagnostic_request", source)
        self.assertIn("hp_modbus_parse_diagnostic_words", source)
        self.assertIn("return !words->empty();", source)
        self.assertIn('publish_state("No response")', source)
        self.assertIn("hp_modbus_publish_diagnostic_response(bytes, start, end)", source)


if __name__ == "__main__":
    unittest.main()
