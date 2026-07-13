#pragma once

#include <cstdint>
#include <string>

struct RegMeta {
  const char *name;
  float scale;
  uint8_t value_type;
  const char *unit;
  bool known;
};

constexpr uint8_t VT_U16 = 0;
constexpr uint8_t VT_S16 = 1;
constexpr uint8_t VT_U32 = 2;
constexpr uint8_t VT_S32 = 3;
constexpr uint8_t VT_B16 = 4;
constexpr uint8_t VT_B32 = 5;
constexpr uint8_t VT_ASCII = 6;

inline RegMeta unknown_reg_meta() {
  return {"Unknown", 1.0f, VT_U16, "", false};
}

inline RegMeta lookup_reg_id5(uint16_t reg) {
  // Identity / String Registers (Battery side)
  if (reg >= 30434 && reg <= 30443) return {"BCUSerialNumber", 1.0f, VT_ASCII, "", true};
  if (reg >= 31590 && reg <= 31604) return {"BCUModel", 1.0f, VT_ASCII, "", true};
  if (reg >= 32000 && reg <= 32030) return {"BatteryModule1FirmwareVersion", 1.0f, VT_ASCII, "", true};
  if (reg >= 32150 && reg <= 32164) return {"BatteryModule2FirmwareVersion", 1.0f, VT_ASCII, "", true};
  if (reg >= 32300 && reg <= 32314) return {"BatteryModule3FirmwareVersion", 1.0f, VT_ASCII, "", true};
  if (reg >= 40235 && reg <= 40244) return {"BatteryRegionString", 1.0f, VT_ASCII, "", true};
  if (reg >= 41021 && reg <= 41035) return {"Pack1Model", 1.0f, VT_ASCII, "", true};
  if (reg >= 41171 && reg <= 41185) return {"Pack2Model", 1.0f, VT_ASCII, "", true};
  if (reg >= 41321 && reg <= 41335) return {"Pack3Model", 1.0f, VT_ASCII, "", true};

  switch (reg) {
    
    case 30101: return {"BCU1QuantityOfWorkingPacks", 1.0f, VT_U16, "", true};
    case 30102: return {"StorageBusVoltage", 10.0f, VT_U16, "V", true};
    case 30103: return {"BCU1RackCurrent", 10.0f, VT_S16, "A", true};
    case 30104: return {"BCU1SOC", 1.0f, VT_U16, "%", true};
    case 30105: return {"BCU1SOH", 1.0f, VT_U16, "%", true};
    case 30107: return {"BCU1ChargeDischargePower", 1.0f, VT_S16, "W", true};
    case 30160: return {"BCU1InternalStatusA", 1.0f, VT_U16, "", true};
    case 30161: return {"BCU1InternalStatusB", 1.0f, VT_S16, "", true};
    case 30163: return {"BCU1InternalStatusC", 1.0f, VT_S16, "", true};
    case 30164: return {"BCU1SOE", 1.0f, VT_U16, "%", true};
    case 30167: return {"BCU1DOD", 1.0f, VT_U16, "%", true};
    case 30168: return {"BCU1ChargeableCapacity", 1000.0f, VT_U32, "kWh", true};
    case 30170: return {"BCU1DischargeableCapacity", 1000.0f, VT_U32, "kWh", true};
    case 30176: return {"BCU1HighestPackTemperature", 100.0f, VT_S16, "C", true};
    case 30177: return {"BCU1PackWithHighestTemperature", 1.0f, VT_U16, "", true};
    case 30178: return {"BCU1LowestPackTemperature", 100.0f, VT_S16, "C", true};
    case 30179: return {"BCU1PackWithLowestTemperature", 1.0f, VT_U16, "", true};
    case 30180: return {"BCU1LowestPackVoltage", 10.0f, VT_U16, "V", true};
    case 30181: return {"BCU1PackWithLowestVoltage", 1.0f, VT_U16, "", true};
    case 30182: return {"BCU1HighestPackVoltage", 10.0f, VT_U16, "V", true};
    case 30183: return {"BCU1PackWithHighestVoltage", 1.0f, VT_U16, "", true};
    case 30192: return {"BCU1EnergyChargedToday", 100.0f, VT_U32, "kWh", true};
    case 30194: return {"BCU1EnergyChargedThisMonth", 100.0f, VT_U32, "kWh", true};
    case 30196: return {"BCU1EnergyChargedThisYear", 100.0f, VT_U32, "kWh", true};
    case 30202: return {"BCU1EnergyDischargedToday", 100.0f, VT_U32, "kWh", true};
    case 30204: return {"BCU1EnergyDischargedThisMonth", 100.0f, VT_U32, "kWh", true};
    case 30206: return {"BCU1EnergyDischargedThisYear", 100.0f, VT_U32, "kWh", true};

    /*
    case 30501: return {"BCU2QuantityOfWorkingPacks", 1.0f, VT_U16, "", true};
    case 30502: return {"BCU2DeviceStatus", 1.0f, VT_U16, "", true};
    case 30503: return {"BCU2RackVoltage", 10.0f, VT_S16, "V", true};
    case 30504: return {"BCU2RackCurrent", 10.0f, VT_S16, "A", true};
    case 30505: return {"BCU2SOC", 1.0f, VT_U16, "%", true};
    case 30506: return {"BCU2SOH", 1.0f, VT_U16, "%", true};
    case 30564: return {"BCU2SOE", 1.0f, VT_U16, "%", true};
    case 30567: return {"BCU2DOD", 1.0f, VT_U16, "%", true};
    case 30568: return {"BCU2ChargeableCapacity", 1000.0f, VT_U32, "kWh", true};
    case 30570: return {"BCU2DischargeableCapacity", 1000.0f, VT_U32, "kWh", true};
    case 30572: return {"BCU2ChargeablePower", 1000.0f, VT_U32, "kW", true};
    case 30574: return {"BCU2DischargeablePower", 1000.0f, VT_U32, "kW", true};
    case 30576: return {"BCU2HighestPackTemperature", 100.0f, VT_S16, "C", true};
    case 30577: return {"BCU2PackWithHighestTemperature", 1.0f, VT_U16, "", true};
    case 30578: return {"BCU2LowestPackTemperature", 100.0f, VT_S16, "C", true};
    case 30579: return {"BCU2PackWithLowestTemperature", 1.0f, VT_U16, "", true};
    case 30580: return {"BCU2LowestPackVoltage", 10.0f, VT_U16, "V", true};
    case 30581: return {"BCU2PackWithLowestVoltage", 1.0f, VT_U16, "", true};
    case 30582: return {"BCU2HighestPackVoltage", 10.0f, VT_U16, "V", true};
    case 30583: return {"BCU2PackWithHighestVoltage", 1.0f, VT_U16, "", true};
    case 30592: return {"BCU2EnergyChargedToday", 100.0f, VT_U32, "kWh", true};
    case 30594: return {"BCU2EnergyChargedThisMonth", 100.0f, VT_U32, "kWh", true};
    case 30596: return {"BCU2EnergyChargedThisYear", 100.0f, VT_U32, "kWh", true};
    case 30602: return {"BCU2EnergyDischargedToday", 100.0f, VT_U32, "kWh", true};
    case 30604: return {"BCU2EnergyDischargedThisMonth", 100.0f, VT_U32, "kWh", true};
    case 30606: return {"BCU2EnergyDischargedThisYear", 100.0f, VT_U32, "kWh", true};
    */

    case 31561: return {"DCDCBatterySideVoltage", 10.0f, VT_S16, "V", true};
    case 31562: return {"DCDCBusSideVoltage", 10.0f, VT_S16, "V", true};
    case 31563: return {"DCDCBatterySideCurrent", 10.0f, VT_S16, "A", true};
    case 31564: return {"DCDCBusSideCurrent", 10.0f, VT_S16, "A", true};
    case 31565: return {"DCDCCabinetTemperature", 10.0f, VT_S16, "C", true};
    case 31571: return {"DCDCISOInsulationResistance", 1000.0f, VT_U16, "MOhm", true};
    //case 32057: return {"BatteryTemperature", 10.0f, VT_S16, "C", true};

    // Aliases note:
    // 32352/32353/32355/32356 are the same numeric values as
    // 0x7E60/0x7E61/0x7E63/0x7E64. Keep one canonical case per switch.

    // Battery module 1 telemetry
    case 32051: return {"BatteryModule1State", 1.0f, VT_U16, "", true};
    case 32052: return {"BatteryModule1Voltage", 10.0f, VT_U16, "V", true};
    case 32053: return {"BatteryModule1Current", 10.0f, VT_S16, "A", true};
    case 32054: return {"BatteryModule1WorkingStatus", 1.0f, VT_U16, "", true};
    case 32055: return {"BatteryModule1ChargeDischargePower", 1.0f, VT_S16, "W", true};
    case 32056: return {"BatteryModule1MaxTemperature", 10.0f, VT_U16, "C", true};
    case 32057: return {"BatteryModule1MinTemperature", 10.0f, VT_U16, "C", true};
    case 32058: return {"BatteryModule1SoC", 1.0f, VT_U16, "%", true};

    // Battery module 2 telemetry
    case 32201: return {"BatteryModule2State", 1.0f, VT_U16, "", true};
    case 32202: return {"BatteryModule2Voltage", 10.0f, VT_U16, "V", true};
    case 32203: return {"BatteryModule2Current", 10.0f, VT_S16, "A", true};
    case 32204: return {"BatteryModule2WorkingStatus", 1.0f, VT_U16, "", true};
    case 32205: return {"BatteryModule2ChargeDischargePower", 1.0f, VT_S16, "W", true};
    case 32206: return {"BatteryModule2MaxTemperature", 10.0f, VT_U16, "C", true};
    case 32207: return {"BatteryModule2MinTemperature", 10.0f, VT_U16, "C", true};
    case 32208: return {"BatteryModule2SoC", 1.0f, VT_U16, "%", true};

    // Battery module 3 telemetry
    case 32351: return {"BatteryModule3State", 1.0f, VT_U16, "", true};
    case 32352: return {"BatteryModule3Voltage", 10.0f, VT_U16, "V", true};
    case 32353: return {"BatteryModule3Current", 10.0f, VT_S16, "A", true};
    case 32354: return {"BatteryModule3WorkingStatus", 1.0f, VT_U16, "", true};
    case 32355: return {"BatteryModule3ChargeDischargePower", 1.0f, VT_S16, "W", true};
    case 32356: return {"BatteryModule3MaxTemperature", 10.0f, VT_U16, "C", true};
    case 32357: return {"BatteryModule3MinTemperature", 10.0f, VT_U16, "C", true};
    case 32358: return {"BatteryModule3SoC", 1.0f, VT_U16, "%", true};

    case 39001: return {"DCDC1OperatingState", 1.0f, VT_U16, "", true};
    case 39002: return {"DCDC1ISOInsulationResistance", 1000.0f, VT_U16, "MOhm", true};
    case 39003: return {"DCDC1BatterySideVoltage", 10.0f, VT_S16, "V", true};
    case 39004: return {"DCDC1BusSideVoltage", 10.0f, VT_S16, "V", true};
    case 39005: return {"DCDC1BatterySideCurrent", 10.0f, VT_S16, "A", true};
    case 39006: return {"DCDC1BusSideCurrent", 10.0f, VT_S16, "A", true};
    case 39007: return {"DCDC2ISOInsulationResistance", 1000.0f, VT_U16, "MOhm", true};
    case 39008: return {"DCDC2BatterySideVoltage", 10.0f, VT_S16, "V", true};
    case 39009: return {"DCDC2BusSideVoltage", 10.0f, VT_S16, "V", true};
    case 39010: return {"DCDC2BatterySideCurrent", 10.0f, VT_S16, "A", true};
    case 39011: return {"DCDC2BusSideCurrent", 10.0f, VT_S16, "A", true};
    case 39012: return {"DCDC1RunningStatus", 1.0f, VT_U16, "", true};
    case 39013: return {"DCDC2RunningStatus", 1.0f, VT_U16, "", true};

    case 39503: return {"StorageUnitRatedCapacity", 1000.0f, VT_U16, "kWh", true};
    default: return unknown_reg_meta();
  }
}

inline RegMeta lookup_reg_id11(uint16_t reg) {
  // Utility meter telemetry (ID 11)
  if (reg == 2214 || reg == 2215) return {"MeterTelemetryFloat1", 1.0f, VT_U32, "", true};
  if (reg == 2216 || reg == 2217) return {"MeterTelemetryFloat2", 1.0f, VT_U32, "", true};
  if (reg == 2218 || reg == 2219) return {"MeterTelemetryFloat3", 1.0f, VT_U32, "", true};
  if (reg == 2102 || reg == 2103) return {"MeterTelemetryFloat4", 1.0f, VT_U32, "", true};
  if (reg == 2104 || reg == 2105) return {"MeterTelemetryFloat5", 1.0f, VT_U32, "", true};
  if (reg == 2106 || reg == 2107) return {"MeterTelemetryFloat6", 1.0f, VT_U32, "", true};

  switch (reg) {
    default: return unknown_reg_meta();
  }
}

inline RegMeta lookup_reg_shared(uint16_t reg) {
  // Shared inverter telemetry that can appear independently of battery/meter unit split.
  switch (reg) {
    case 32066: return {"LineVoltageBetweenPhasesAAndB", 10.0f, VT_U16, "V", true};
    case 32067: return {"LineVoltageBetweenPhasesBAndC", 10.0f, VT_U16, "V", true};
    case 32068: return {"LineVoltageBetweenPhasesCAndA", 10.0f, VT_U16, "V", true};
    case 32069: return {"PhaseAVoltage", 10.0f, VT_U16, "V", true};
    case 32070: return {"PhaseBVoltage", 10.0f, VT_U16, "V", true};
    case 32071: return {"PhaseCVoltage", 10.0f, VT_U16, "V", true};
    case 32072: return {"PhaseACurrent", 1000.0f, VT_S32, "A", true};
    case 32074: return {"PhaseBCurrent", 1000.0f, VT_S32, "A", true};
    case 32076: return {"PhaseCCurrent", 1000.0f, VT_S32, "A", true};
    case 32078: return {"PeakActivePowerOfCurrentDay", 1.0f, VT_S32, "W", true};
    case 32080: return {"ActivePower", 1.0f, VT_S32, "W", true};
    case 32084: return {"PowerFactor", 1000.0f, VT_S16, "", true};
    case 32085: return {"GridFrequency", 100.0f, VT_U16, "Hz", true};
    case 32087: return {"InternalTemperature", 10.0f, VT_S16, "C", true};
    case 32088: return {"InsulationResistance", 1000.0f, VT_U16, "MOhm", true};
    case 32089: return {"DeviceStatus", 1.0f, VT_U16, "", true};
    case 32090: return {"FaultCode", 1.0f, VT_U16, "", true};
    case 32106: return {"AccumulatedEnergyYield", 100.0f, VT_U32, "kWh", true};
    case 32114: return {"DailyEnergyYield", 100.0f, VT_U32, "kWh", true};
    case 37000: return {"Unit1RunningStatus", 1.0f, VT_U16, "", true};
    case 37001: return {"Unit1ChargeAndDischargePower", 1.0f, VT_S32, "W", true};
    case 37003: return {"Unit1BusVoltage", 10.0f, VT_U16, "V", true};
    case 37004: return {"Unit1BatterySOC", 10.0f, VT_U16, "%", true};
    case 37021: return {"Unit1BusCurrent", 10.0f, VT_S16, "A", true};
    case 37738: return {"Unit2BatterySOC", 10.0f, VT_U16, "%", true};
    case 37741: return {"Unit2RunningStatus", 1.0f, VT_U16, "", true};
    case 37743: return {"Unit2ChargeAndDischargePower", 1.0f, VT_S32, "W", true};
    case 37750: return {"Unit2BusVoltage", 10.0f, VT_U16, "V", true};
    case 37751: return {"Unit2BusCurrent", 10.0f, VT_S16, "A", true};
    case 37760: return {"SOC", 10.0f, VT_U16, "%", true};
    case 37763: return {"BusVoltage", 10.0f, VT_U16, "V", true};
    case 37764: return {"BusCurrent", 10.0f, VT_S16, "A", true};
    case 37765: return {"ChargeDischargePower", 1.0f, VT_S32, "W", true};
    default: return unknown_reg_meta();
  }
}

inline RegMeta lookup_reg(uint8_t addr, uint16_t reg) {
  if (addr == 5) {
    RegMeta meta = lookup_reg_id5(reg);
    if (meta.known) return meta;
  }

  if (addr == 11) {
    RegMeta meta = lookup_reg_id11(reg);
    if (meta.known) return meta;
  }

  RegMeta shared = lookup_reg_shared(reg);
  if (shared.known) return shared;

  return unknown_reg_meta();
}

inline void update_sensor_for_reg(uint8_t addr, uint16_t r) {
  switch (r) {
    case 30102: id(s_bcu1_rack_voltage).update(); break;
    case 30103: id(s_bcu1_rack_current).update(); break;
    case 30104:
      id(s_bcu1soh).update();
      id(s_bcu1_residual_capacity).update();
      break;
    case 30105: id(s_bcu1soc).update(); break;
    case 30107: id(s_bcu1_charge_discharge_power).update(); break;
    case 30160: id(s_u75d0).update(); break;
    case 30161: id(s_s75d1).update(); break;
    case 30163: id(s_s75d3).update(); break;
    case 30164: id(s_u75d4).update(); break;
    case 31565: id(s_dcdc_cabinet_temp).update(); break;
    case 32052: id(s_battery_module_1_voltage).update(); break;
    case 32053: id(s_battery_module_1_current).update(); break;
    case 32054: id(ts_battery_module_1_working_status).update(); break;
    case 32055: id(s_battery_module_1_power).update(); break;
    case 32056: id(s_battery_module_1_max_temp).update(); break;
    case 32057: id(s_battery_module_1_min_temp).update(); break;
    case 32058:
      id(s_battery_module_1_soc).update();
      id(s_battery_module_1_residual_capacity).update();
      break;
    case 32202: id(s_battery_module_2_voltage).update(); break;
    case 32203: id(s_battery_module_2_current).update(); break;
    case 32204: id(ts_battery_module_2_working_status).update(); break;
    case 32205: id(s_battery_module_2_power).update(); break;
    case 32206: id(s_battery_module_2_max_temp).update(); break;
    case 32207: id(s_battery_module_2_min_temp).update(); break;
    case 32208:
      id(s_battery_module_2_soc).update();
      id(s_battery_module_2_residual_capacity).update();
      break;
    case 32352: id(s_battery_module_3_voltage).update(); break;
    case 32353: id(s_battery_module_3_current).update(); break;
    case 32354: id(ts_battery_module_3_working_status).update(); break;
    case 32355: id(s_battery_module_3_power).update(); break;
    case 32356: id(s_battery_module_3_max_temp).update(); break;
    case 32357: id(s_battery_module_3_min_temp).update(); break;
    case 32358:
      id(s_battery_module_3_soc).update();
      id(s_battery_module_3_residual_capacity).update();
      break;
    case 39503: id(s_storage_unit_rated_capacity).update(); break;
    default: break;
  }
  if (addr == 11) {
    if (r == 0x08A6) id(s_id11_08a6_f32).update();
    if (r == 0x08A8) id(s_id11_08a8_f32).update();
    if (r == 0x08AA) id(s_id11_08aa_f32).update();
  }
}

inline void assign_ascii_value(uint8_t addr, uint16_t reg, const std::string &text) {
  if (addr != 5) return;
  if (reg >= 30434 && reg <= 30443) {
    id(g_bcu_regkey) = text;
    id(ts_bcu_regkey).update();
    return;
  }
  if (reg >= 31590 && reg <= 31604) {
    id(g_bcu_model) = text;
    id(ts_bcu_model).update();
    return;
  }
  if (reg >= 32000 && reg <= 32030) {
    id(g_firmware_diag_string) = text;
    id(ts_firmware_diag_string).update();
    return;
  }
  if (reg >= 32150 && reg <= 32164) {
    id(g_firmware_version) = text;
    id(ts_firmware_version).update();
    return;
  }
  if (reg >= 32300 && reg <= 32314) {
    id(g_firmware_version_mirror) = text;
    id(ts_firmware_version_mirror).update();
    return;
  }
  if (reg >= 40235 && reg <= 40244) {
    id(g_region_ascii) = text;
    id(ts_9d2b_ascii).update();
    return;
  }
  if (reg >= 41021 && reg <= 41035) {
    id(g_pack1_model) = text;
    id(ts_pack1_model).update();
    return;
  }
  if (reg >= 41171 && reg <= 41185) {
    id(g_pack2_model) = text;
    id(ts_pack2_model).update();
    return;
  }
  if (reg >= 41321 && reg <= 41335) {
    id(g_pack3_model) = text;
    id(ts_pack3_model).update();
    return;
  }
}

inline std::string decode_ascii(const uint8_t *data, uint16_t count_words, bool swap_bytes) {
  std::string out;
  out.reserve(static_cast<size_t>(count_words) * 2U);
  for (uint16_t aw = 0; aw < count_words; aw++) {
    const uint8_t b0 = data[2 * aw];
    const uint8_t b1 = data[2 * aw + 1];
    const uint8_t first = swap_bytes ? b1 : b0;
    const uint8_t second = swap_bytes ? b0 : b1;
    if (first == 0U) break;
    out.push_back((first >= 32U && first <= 126U) ? static_cast<char>(first) : '.');
    if (second == 0U) break;
    out.push_back((second >= 32U && second <= 126U) ? static_cast<char>(second) : '.');
  }
  return out;
}

inline uint16_t ascii_score(const std::string &text) {
  uint16_t score = 0;
  for (char c : text) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ' ') {
      score++;
    }
  }
  return score;
}

inline std::string parse_ascii_text(const uint8_t *data, uint16_t count_words) {
  const std::string text_normal = decode_ascii(data, count_words, false);
  const std::string text_swapped = decode_ascii(data, count_words, true);
  return ascii_score(text_swapped) > ascii_score(text_normal) ? text_swapped : text_normal;
}

inline void handle_unknown_ascii(uint8_t addr, uint16_t reg, const std::string &text) {
  if (addr == 5 && reg == 0x9D2B) {
    id(g_region_ascii) = text;
  }
  id(g_unknown_ascii) = text;
  id(ts_unknown_ascii).update();
}

inline void handle_unknown_fallback_float(uint16_t first_word) {
  id(g_unknown_fallback_float) = static_cast<float>(static_cast<int16_t>(first_word)) / 10.0f;
  id(s_unknown_fallback_float).update();
}

inline void assign_known_u16_s16(uint8_t dev_addr, uint16_t reg_addr, uint16_t raw_u16, int16_t raw_s16, float scaled) {
  if (dev_addr != 5) return;
  switch (reg_addr) {
    case 30102: id(g_bcu1_rack_voltage) = raw_u16; break;
    case 30103: id(g_bcu1_rack_current) = raw_s16; break;
    case 30104:
      if (raw_u16 <= 100U) id(g_bcu1soc) = raw_u16;
      break;
    case 30105:
      if (raw_u16 <= 100U) id(g_bcu1soh) = raw_u16;
      break;
    case 30107: id(g_bcu1_charge_discharge_power) = scaled; break;
    case 39001: id(g_u9859) = raw_u16; break;
    case 39003: id(g_u985b) = raw_u16; break;
    case 30160: id(g_u75d0) = raw_u16; break;
    case 30161: id(g_s75d1) = raw_s16; break;
    case 30163: id(g_s75d3) = raw_s16; break;
    case 30164: id(g_u75d4) = raw_u16; break;
    case 32052: id(g_battery_module_1_voltage) = scaled; break;
    case 32053: id(g_battery_module_1_current) = scaled; break;
    case 32054: id(g_battery_module_1_working_status) = raw_u16; break;
    case 32055: id(g_battery_module_1_power) = scaled; break;
    case 32056: id(g_battery_module_1_max_temp) = scaled; break;
    case 32057: id(g_battery_module_1_min_temp) = scaled; break;
    case 32058:
      if (raw_u16 <= 100U) id(g_battery_module_1_soc) = raw_u16;
      break;
    case 32202: id(g_battery_module_2_voltage) = scaled; break;
    case 32203: id(g_battery_module_2_current) = scaled; break;
    case 32204: id(g_battery_module_2_working_status) = raw_u16; break;
    case 32205: id(g_battery_module_2_power) = scaled; break;
    case 32206: id(g_battery_module_2_max_temp) = scaled; break;
    case 32207: id(g_battery_module_2_min_temp) = scaled; break;
    case 32208:
      if (raw_u16 <= 100U) id(g_battery_module_2_soc) = raw_u16;
      break;
    case 32352: id(g_battery_module_3_voltage) = scaled; break;
    case 32353: id(g_battery_module_3_current) = scaled; break;
    case 32354: id(g_battery_module_3_working_status) = raw_u16; break;
    case 32355: id(g_battery_module_3_power) = scaled; break;
    case 32356: id(g_battery_module_3_max_temp) = scaled; break;
    case 32357: id(g_battery_module_3_min_temp) = scaled; break;
    case 32358:
      if (raw_u16 <= 100U) id(g_battery_module_3_soc) = raw_u16;
      break;
    case 40160: id(g_u9ce0) = raw_u16; break;
    case 40161: id(g_u9ce1) = raw_u16; break;
    case 31565: id(g_dcdc_cabinet_temp) = scaled; break;
    case 39503:
      if (scaled >= 1.0f && scaled <= 50.0f) id(g_storage_unit_rated_capacity) = scaled;
      break;
   default: break;
  }
}

inline void assign_known_u32(uint8_t dev_addr, uint16_t reg_addr, float scaled_signed, float scaled_unsigned) {
  (void) scaled_unsigned;

  if (dev_addr != 5) return;

  switch (reg_addr) {
    // Keep signed interpretation for power-like values.
    case 32082: id(g_reactive_power) = scaled_signed; break;
    default: break;
  }
}


inline void assign_id11_pair_float(uint16_t rp, float value) {
  switch (rp) {
    case 0x08A6:
      id(g_id11_08a6_f32) = value;
      id(s_id11_08a6_f32).update();
      break;
    case 0x08A8:
      id(g_id11_08a8_f32) = value;
      id(s_id11_08a8_f32).update();
      break;
    case 0x08AA:
      id(g_id11_08aa_f32) = value;
      id(s_id11_08aa_f32).update();
      break;
    default:
      break;
  }
}

inline void assign_write_single(uint8_t dev_addr, uint16_t reg, uint16_t val) {
  if (dev_addr != 5) return;
  switch (reg) {
    case 0xBF68: id(g_cmd_bf68) = val; break;
    case 0x9D87: id(g_cmd_9d87) = val; break;
    case 0x9D12: id(g_cmd_9d12) = val; break;
    case 0xBF71: id(g_cmd_bf71) = val; break;
    case 0xC032: id(g_cmd_c032) = val; break;
    case 0x9D4F: id(g_cmd_9d4f) = val; break;
    case 0x9D56: id(g_cmd_9d56) = val; break;
    default: break;
  }
  update_sensor_for_reg(dev_addr, reg);
}
