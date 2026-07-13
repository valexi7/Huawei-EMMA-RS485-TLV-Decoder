#include "esphome.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

static const char *const HUAWEI_PROP_TAG = "huawei_prop";

static constexpr uint16_t HP_TAG_LINE_VOLTAGE = 0x774D;       // A-B, B-C, C-A line voltages (×0.1 V)
static constexpr uint16_t HP_TAG_PHASE_VOLTAGE = 0x9C52;      // L-N phase voltages A/B/C (×0.1 V)
static constexpr uint16_t HP_TAG_GRID_FREQ_PHASE_V = 0x7733;  // count=4: freq(/100 Hz), A/B/C V (×0.1)
static constexpr uint16_t HP_TAG_PHASE_VOLTAGE_COUNT = 0x7734;  // count=3 L-N A/B/C (×0.1 V), EMMA + others
static constexpr uint16_t HP_TAG_PHASE_VOLTAGE_TLV = 0x7EC5;
static constexpr uint16_t HP_TAG_PHASE_VOLTAGE_U32 = 0x90ED;
static constexpr uint16_t HP_TAG_PHASE_CURRENT = 0x7D48;    // 0x35 words=6, A/B/C amps (×0.001)
static constexpr uint16_t HP_TAG_PHASE_ACTIVE_POWER = 0x910C;  // 0x35 words=6, Pa/Pb/Pc (W)
static constexpr uint16_t HP_TAG_EMMA_PHASE_VOLTAGE = 0x7729;  // EMMA count=3 L-N A/B/C (×0.1 V)
static constexpr uint16_t HP_TAG_LINE_VOLTAGE_U32 = 0x9106;    // u32 line-voltage payload (×0.1 V)
static constexpr uint16_t HP_TAG_EMMA_PHASE_ACTIVE_POWER = 0x90FB;  // 0x35 words=3 i16 Pa/Pb/Pc (W)
static constexpr uint16_t HP_TAG_EMMA_PHASE_ACTIVE_POWER_I32 = 0x90F3;  // 0x35 words=6 i32 Pa/Pb/Pc (W)
static constexpr uint16_t HP_TAG_ACTIVE_POWER = 0x7D5F;     // 0x35 words=2 i32, active power W
static constexpr uint16_t HP_TAG_REACTIVE_POWER = 0x7D66;   // 0x35 words=2, var (signed)
static constexpr uint16_t HP_TAG_REACTIVE_AUX = 0x93C5;     // 0x35 words=2, var or PF-related (signed)
static constexpr uint16_t HP_TAG_TOTAL_YIELD = 0x7D6A;      // 0x35 words=2 u32, lifetime kWh (×0.01)
static constexpr uint16_t HP_TAG_DAILY_ENERGY = 0x7D80;     // 0x35 words=2 u32, daily kWh (×0.01)
static constexpr uint16_t HP_TAG_INVERTER_TIMES = 0x7D01;   // 0x35 words=6 u32 startup/shutdown/unknown
static constexpr uint16_t HP_TAG_BATTERY_CHARGED_TODAY = 0x9097;   // 0x35 words=2 u32, kWh (×0.01)
static constexpr uint16_t HP_TAG_BATTERY_DISCHARGED_TODAY = 0x9398;  // 0x35 words=2 u32, kWh (×0.01)
static constexpr uint16_t HP_TAG_BATTERY_TOTAL_CHARGE = 0x9394;    // 0x35 words=2 u32, lifetime kWh (×0.01)
static constexpr uint16_t HP_TAG_BATTERY_TOTAL_DISCHARGE = 0x90CA;  // 0x35 words=10, discharge u32 at byte +5
static constexpr uint16_t HP_TAG_BATTERY_POWER = 0x9089;           // 0x35 words=2 i32, charge/discharge W
static constexpr size_t HP_BATTERY_TOTAL_DISCHARGE_OPAQUE_OFFSET = 5;
static constexpr uint16_t HP_TAG_INVERTER_SOLAR_PV = 0x7E44;         // 0x35 words=4 i32[2], PV1/PV2 watts
static constexpr uint16_t HP_TAG_EMMA_ACTIVE_POWER_W = 0x9385;   // 0x35 words=2 i32, grid active power W
static constexpr uint16_t HP_TAG_BUS_CLOCK = 0x7D6E;             // variable words: u32 unix sec, optional u32 seq
static constexpr uint16_t HP_TAG_SERIAL_NUMBER = 0x9187;         // word payload containing ASCII serial number
static constexpr float HP_ENERGY_YIELD_SCALE = 0.01f;
static constexpr float HP_LINE_VOLTAGE_SCALE = 0.1f;
static constexpr float HP_PHASE_VOLTAGE_SCALE = 0.1f;
static constexpr float HP_PHASE_CURRENT_SCALE = 0.001f;
static constexpr float HP_GRID_FREQUENCY_SCALE = 0.01f;
static constexpr size_t HP_DISCOVERED_DEVICE_SLOTS = 16;

static constexpr uint8_t HP_FC41_SUBFN_UPLOAD_START = 0x05;
static constexpr uint8_t HP_FC41_SUBFN_UPLOAD_DATA = 0x06;
static constexpr uint8_t HP_FC41_SUBFN_UPLOAD_COMPLETE = 0x0C;
static constexpr uint8_t HP_FC41_SUBFN_CURRENT_DATA = 0x35;
static constexpr uint8_t HP_FC41_SUBFN_REPORT = 0x89;
static constexpr uint8_t HP_FC41_EXCEPTION_FUNCTION = 0xC1;

// FC41 encodes startup/shutdown as seconds before a fixed local-time epoch (matches Modbus 32091/32093).
static constexpr uint32_t HP_FC41_TIME_UNIX_OFFSET = 1756181774UL;
static constexpr uint32_t HP_FC41_TIME_RAW_MAX = 600000000UL;

// Known FC41 device roles (extend when more IDs are identified).
static constexpr uint8_t HP_DEV_EMMA = 0;
static constexpr uint8_t HP_DEV_METER = 2;
static constexpr uint8_t HP_DEV_INVERTER = 12;

static inline const char *hp_device_role_name(uint8_t device_id) {
  switch (device_id) {
    case HP_DEV_EMMA:
      return "EMMA";
    case HP_DEV_METER:
      return "Power Meter";
    case HP_DEV_INVERTER:
      return "Inverter";
    default:
      return nullptr;
  }
}

struct HpPfState {
  float p_watts{0.0f};
  float q_var{0.0f};
  bool have_p{false};
  bool have_q{false};
};

static HpPfState hp_pf_emma;
static HpPfState hp_pf_inverter;

static uint8_t hp_discovered_devices[HP_DISCOVERED_DEVICE_SLOTS];
static size_t hp_discovered_device_count = 0;

static inline bool hp_tlv_type_size(uint8_t type, size_t *out_size);
static inline uint32_t hp_u32be(const std::vector<uint8_t> &bytes, size_t pos);

static inline bool hp_fc41_time_raw_looks_valid(uint32_t raw) {
  return raw > 0 && raw < HP_FC41_TIME_RAW_MAX;
}

static inline bool hp_unix_time_looks_valid(uint32_t unix_sec) {
  // 2020-01-01 .. 2040-01-01 UTC
  return unix_sec >= 1577836800UL && unix_sec < 2208988800UL;
}

static inline void hp_format_unix_local_time(uint32_t unix_sec, char *out, size_t out_size) {
  if (!hp_unix_time_looks_valid(unix_sec)) {
    snprintf(out, out_size, "raw=%lu (0x%08lX)", static_cast<unsigned long>(unix_sec),
             static_cast<unsigned long>(unix_sec));
    return;
  }
  const time_t t = static_cast<time_t>(unix_sec);
  struct tm tm_buf;
  if (localtime_r(&t, &tm_buf) == nullptr) {
    snprintf(out, out_size, "raw=%lu", static_cast<unsigned long>(unix_sec));
    return;
  }
  char time_buf[32];
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
  snprintf(out, out_size, "%s", time_buf);
}

static inline void hp_format_fc41_local_time(uint32_t raw, char *out, size_t out_size) {
  if (raw == 0) {
    snprintf(out, out_size, "unset");
    return;
  }
  if (!hp_fc41_time_raw_looks_valid(raw)) {
    snprintf(out, out_size, "raw=%lu (0x%08lX)", static_cast<unsigned long>(raw), static_cast<unsigned long>(raw));
    return;
  }
  const time_t t = static_cast<time_t>(static_cast<uint64_t>(raw) + HP_FC41_TIME_UNIX_OFFSET);
  struct tm tm_buf;
  if (localtime_r(&t, &tm_buf) == nullptr) {
    snprintf(out, out_size, "raw=%lu", static_cast<unsigned long>(raw));
    return;
  }
  char time_buf[32];
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
  snprintf(out, out_size, "%s", time_buf);
}

static inline void hp_publish_total_yield(uint8_t device_id, uint32_t raw_wh) {
  const float kwh = static_cast<float>(raw_wh) * HP_ENERGY_YIELD_SCALE;
  switch (device_id) {
    case HP_DEV_EMMA:
      id(emma_total_yield).publish_state(kwh);
      break;
    case HP_DEV_INVERTER:
      id(inverter_total_yield).publish_state(kwh);
      break;
    default:
      break;
  }
}

static inline void hp_publish_daily_energy(uint8_t device_id, uint32_t raw_wh) {
  const float kwh = static_cast<float>(raw_wh) * HP_ENERGY_YIELD_SCALE;
  switch (device_id) {
    case HP_DEV_EMMA:
      id(emma_daily_energy).publish_state(kwh);
      break;
    case HP_DEV_INVERTER:
      id(inverter_daily_energy).publish_state(kwh);
      break;
    default:
      break;
  }
}

static inline void hp_try_publish_bus_clock_value(uint8_t device_id, uint16_t tag,
                                                  const std::vector<uint8_t> &bytes, size_t pos,
                                                  size_t val_size) {
  if (tag != HP_TAG_BUS_CLOCK || val_size < 4 || pos + 3 + val_size > bytes.size())
    return;

  const uint32_t unix_sec = hp_u32be(bytes, pos + 3);
  const bool have_sequence = val_size >= 8;
  const uint32_t sequence = have_sequence ? hp_u32be(bytes, pos + 7) : 0;

  char buf[48];
  hp_format_unix_local_time(unix_sec, buf, sizeof(buf));

  switch (device_id) {
    case HP_DEV_INVERTER:
      id(inverter_bus_clock).publish_state(buf);
      if (have_sequence)
        id(inverter_bus_clock_sequence).publish_state(static_cast<float>(sequence));
      break;
    case HP_DEV_EMMA:
      id(emma_bus_clock).publish_state(buf);
      if (have_sequence)
        id(emma_bus_clock_sequence).publish_state(static_cast<float>(sequence));
      break;
    default:
      break;
  }
}

static inline void hp_publish_inverter_times(uint8_t device_id, uint32_t startup, uint32_t shutdown, uint32_t field3) {
  if (device_id != HP_DEV_INVERTER)
    return;

  char buf[48];
  hp_format_fc41_local_time(startup, buf, sizeof(buf));
  id(inverter_startup_time).publish_state(buf);
  hp_format_fc41_local_time(shutdown, buf, sizeof(buf));
  id(inverter_shutdown_time).publish_state(buf);

  if (hp_fc41_time_raw_looks_valid(field3)) {
    hp_format_fc41_local_time(field3, buf, sizeof(buf));
    id(inverter_time_field3).publish_state(buf);
  } else {
    snprintf(buf, sizeof(buf), "raw=%lu (0x%08lX)", static_cast<unsigned long>(field3),
             static_cast<unsigned long>(field3));
    id(inverter_time_field3).publish_state(buf);
  }
}

static inline void hp_note_device_id(uint8_t device_id) {
  for (size_t i = 0; i < hp_discovered_device_count; i++) {
    if (hp_discovered_devices[i] == device_id)
      return;
  }
  if (hp_discovered_device_count >= HP_DISCOVERED_DEVICE_SLOTS)
    return;

  hp_discovered_devices[hp_discovered_device_count++] = device_id;
}

static inline void hp_publish_discovered_device_ids() {
  if (hp_discovered_device_count == 0) {
    id(huawei41_device_ids).publish_state("(none yet)");
    return;
  }

  uint8_t sorted[HP_DISCOVERED_DEVICE_SLOTS];
  for (size_t i = 0; i < hp_discovered_device_count; i++)
    sorted[i] = hp_discovered_devices[i];

  for (size_t i = 0; i + 1 < hp_discovered_device_count; i++) {
    for (size_t j = i + 1; j < hp_discovered_device_count; j++) {
      if (sorted[j] < sorted[i]) {
        const uint8_t tmp = sorted[i];
        sorted[i] = sorted[j];
        sorted[j] = tmp;
      }
    }
  }

  std::string out;
  char buf[40];
  for (size_t i = 0; i < hp_discovered_device_count; i++) {
    const char *role = hp_device_role_name(sorted[i]);
    if (role != nullptr) {
      snprintf(buf, sizeof(buf), "%u (0x%02X) %s", sorted[i], sorted[i], role);
    } else {
      snprintf(buf, sizeof(buf), "%u (0x%02X)", sorted[i], sorted[i]);
    }
    if (!out.empty())
      out += ", ";
    out += buf;
  }

  id(huawei41_device_ids).publish_state(out);
}

static inline uint16_t hp_u16be(uint8_t hi, uint8_t lo) {
  return (static_cast<uint16_t>(hi) << 8) | lo;
}

static inline uint32_t hp_u32be(const std::vector<uint8_t> &bytes, size_t pos) {
  return (static_cast<uint32_t>(bytes[pos]) << 24) | (static_cast<uint32_t>(bytes[pos + 1]) << 16) |
         (static_cast<uint32_t>(bytes[pos + 2]) << 8) | static_cast<uint32_t>(bytes[pos + 3]);
}

static inline int32_t hp_i32be(const std::vector<uint8_t> &bytes, size_t pos) {
  return static_cast<int32_t>(hp_u32be(bytes, pos));
}

static inline std::string hp_hex_slice(const std::vector<uint8_t> &bytes, size_t start, size_t len) {
  std::string out;
  char buf[4];
  const size_t end = std::min(bytes.size(), start + len);
  for (size_t i = start; i < end; i++) {
    snprintf(buf, sizeof(buf), "%02X ", bytes[i]);
    out += buf;
  }
  if (!out.empty() && out.back() == ' ')
    out.pop_back();
  return out;
}

static inline void hp_log_hex_chunks(const std::vector<uint8_t> &bytes, size_t start, size_t len,
                                     const char *label) {
  static constexpr size_t HP_LOG_HEX_CHUNK_BYTES = 32;
  const size_t end = std::min(bytes.size(), start + len);
  for (size_t pos = start; pos < end; pos += HP_LOG_HEX_CHUNK_BYTES) {
    const size_t chunk_len = std::min(HP_LOG_HEX_CHUNK_BYTES, end - pos);
    ESP_LOGI(HUAWEI_PROP_TAG, "  %s[%u..%u]=%s", label, static_cast<unsigned>(pos - start),
             static_cast<unsigned>(pos - start + chunk_len - 1), hp_hex_slice(bytes, pos, chunk_len).c_str());
  }
}

static inline const char *hp_subfunction_name(uint8_t subfunction) {
  switch (subfunction) {
    case HP_FC41_SUBFN_UPLOAD_START:
      return "upload-start";
    case HP_FC41_SUBFN_UPLOAD_DATA:
      return "upload-data";
    case HP_FC41_SUBFN_UPLOAD_COMPLETE:
      return "upload-complete";
    case HP_FC41_SUBFN_REPORT:
      return "report";
    case HP_FC41_SUBFN_CURRENT_DATA:
      return "current-sensor-data";
    case 0x33:
      return "query";
    default:
      return "unknown";
  }
}

static inline bool hp_is_upload_subfunction(uint8_t subfunction) {
  return subfunction == HP_FC41_SUBFN_UPLOAD_START || subfunction == HP_FC41_SUBFN_UPLOAD_DATA ||
         subfunction == HP_FC41_SUBFN_UPLOAD_COMPLETE;
}

static inline const char *hp_modbus_exception_name(uint8_t code) {
  switch (code) {
    case 0x01:
      return "illegal-function";
    case 0x02:
      return "illegal-data-address";
    case 0x03:
      return "illegal-data-value";
    case 0x04:
      return "server-device-failure";
    case 0x05:
      return "acknowledge";
    case 0x06:
      return "server-device-busy";
    case 0x08:
      return "memory-parity-error";
    case 0x0A:
      return "gateway-path-unavailable";
    case 0x0B:
      return "gateway-target-no-response";
    default:
      return "unknown";
  }
}

static inline uint16_t hp_modbus_crc(const std::vector<uint8_t> &bytes, size_t start, size_t len) {
  uint16_t crc = 0xFFFF;
  const size_t end = std::min(bytes.size(), start + len);
  for (size_t pos = start; pos < end; pos++) {
    crc ^= bytes[pos];
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001) : static_cast<uint16_t>(crc >> 1);
  }
  return crc;
}

static inline bool hp_modbus_crc_matches(const std::vector<uint8_t> &bytes, size_t start, size_t end) {
  if (end > bytes.size() || end < start + 4)
    return false;
  const uint16_t expected = static_cast<uint16_t>(bytes[end - 2]) |
                            (static_cast<uint16_t>(bytes[end - 1]) << 8);
  return hp_modbus_crc(bytes, start, end - start - 2) == expected;
}

static inline bool hp_is_count_u16_tag(uint16_t tag) {
  return (tag >= 0x7700 && tag <= 0x77FF) || tag == HP_TAG_PHASE_VOLTAGE;
}

static inline bool hp_tlv_tag_looks_valid(uint16_t tag) {
  if (tag < 0x0100)
    return false;
  if (hp_is_count_u16_tag(tag))
    return true;
  if (tag >= 0x7D00 && tag <= 0x7EFF)
    return true;
  if (tag >= 0x9000 && tag <= 0x93FF)
    return true;
  if (tag >= 0x9100 && tag <= 0x91FF)
    return true;
  return false;
}

static inline int16_t hp_i16be(uint8_t hi, uint8_t lo) {
  return static_cast<int16_t>(hp_u16be(hi, lo));
}

static inline bool hp_tlv_type_size(uint8_t type, size_t *out_size) {
  switch (type) {
    case 0x01:
      *out_size = 1;
      return true;
    case 0x02:
      *out_size = 4;
      return true;
    case 0x03:
      *out_size = 6;
      return true;
    case 0x04:
      *out_size = 8;
      return true;
    case 0x08:
      *out_size = 8;
      return true;
    case 0x06:
      *out_size = 12;
      return true;
    case 0x0A:
      *out_size = 10;
      return true;
    case 0x0C:
      *out_size = 12;
      return true;
    default:
      return false;
  }
}

static inline bool hp_find_count_u16_tag(const std::vector<uint8_t> &bytes, size_t start, size_t end, uint16_t tag,
                                         uint16_t expected_count, uint16_t *out_values) {
  const uint8_t hi = static_cast<uint8_t>(tag >> 8);
  const uint8_t lo = static_cast<uint8_t>(tag & 0xFF);

  for (size_t k = start; k + 4 + (2 * expected_count) <= end; k++) {
    if (bytes[k] != hi || bytes[k + 1] != lo)
      continue;

    const uint16_t count = hp_u16be(bytes[k + 2], bytes[k + 3]);
    if (count != expected_count)
      continue;

    for (uint16_t n = 0; n < expected_count; n++) {
      out_values[n] = hp_u16be(bytes[k + 4 + (2 * n)], bytes[k + 5 + (2 * n)]);
    }
    return true;
  }
  return false;
}

static inline void hp_publish_line_voltage_sensors(uint8_t device_id, const uint16_t *values) {
  switch (device_id) {
    case HP_DEV_METER:
      id(meter_line_ab_voltage).publish_state(static_cast<float>(values[0]) * HP_LINE_VOLTAGE_SCALE);
      id(meter_line_bc_voltage).publish_state(static_cast<float>(values[1]) * HP_LINE_VOLTAGE_SCALE);
      id(meter_line_ca_voltage).publish_state(static_cast<float>(values[2]) * HP_LINE_VOLTAGE_SCALE);
      break;
    case HP_DEV_EMMA:
      id(emma_line_ab_voltage_tlv).publish_state(static_cast<float>(values[0]) * HP_LINE_VOLTAGE_SCALE);
      id(emma_line_bc_voltage_tlv).publish_state(static_cast<float>(values[1]) * HP_LINE_VOLTAGE_SCALE);
      id(emma_line_ca_voltage_tlv).publish_state(static_cast<float>(values[2]) * HP_LINE_VOLTAGE_SCALE);
      break;
    case HP_DEV_INVERTER:
      id(inverter_line_ab_voltage).publish_state(static_cast<float>(values[0]) * HP_LINE_VOLTAGE_SCALE);
      id(inverter_line_bc_voltage).publish_state(static_cast<float>(values[1]) * HP_LINE_VOLTAGE_SCALE);
      id(inverter_line_ca_voltage).publish_state(static_cast<float>(values[2]) * HP_LINE_VOLTAGE_SCALE);
      break;
    default:
      break;
  }
}

static inline void hp_publish_phase_voltage_sensors(uint8_t device_id, const uint16_t *values) {
  switch (device_id) {
    case HP_DEV_METER:
      id(meter_phase_a_voltage).publish_state(static_cast<float>(values[0]) * HP_PHASE_VOLTAGE_SCALE);
      id(meter_phase_b_voltage).publish_state(static_cast<float>(values[1]) * HP_PHASE_VOLTAGE_SCALE);
      id(meter_phase_c_voltage).publish_state(static_cast<float>(values[2]) * HP_PHASE_VOLTAGE_SCALE);
      break;
    case HP_DEV_INVERTER:
      id(inverter_phase_a_voltage).publish_state(static_cast<float>(values[0]) * HP_PHASE_VOLTAGE_SCALE);
      id(inverter_phase_b_voltage).publish_state(static_cast<float>(values[1]) * HP_PHASE_VOLTAGE_SCALE);
      id(inverter_phase_c_voltage).publish_state(static_cast<float>(values[2]) * HP_PHASE_VOLTAGE_SCALE);
      break;
    case HP_DEV_EMMA:
      break;
    default:
      break;
  }
}

static inline void hp_publish_phase_current_sensors(uint8_t device_id, const int32_t *amps_raw) {
  switch (device_id) {
    case HP_DEV_INVERTER:
      id(inverter_phase_a_current).publish_state(static_cast<float>(amps_raw[0]) * HP_PHASE_CURRENT_SCALE);
      id(inverter_phase_b_current).publish_state(static_cast<float>(amps_raw[1]) * HP_PHASE_CURRENT_SCALE);
      id(inverter_phase_c_current).publish_state(static_cast<float>(amps_raw[2]) * HP_PHASE_CURRENT_SCALE);
      break;
    case HP_DEV_EMMA:
      id(emma_phase_a_current).publish_state(static_cast<float>(amps_raw[0]) * HP_PHASE_CURRENT_SCALE);
      id(emma_phase_b_current).publish_state(static_cast<float>(amps_raw[1]) * HP_PHASE_CURRENT_SCALE);
      id(emma_phase_c_current).publish_state(static_cast<float>(amps_raw[2]) * HP_PHASE_CURRENT_SCALE);
      break;
    default:
      break;
  }
}

static inline void hp_publish_phase_active_power_sensors(uint8_t device_id, const int32_t *watts) {
  switch (device_id) {
    case HP_DEV_INVERTER:
      id(inverter_phase_a_power).publish_state(static_cast<float>(watts[0]));
      id(inverter_phase_b_power).publish_state(static_cast<float>(watts[1]));
      id(inverter_phase_c_power).publish_state(static_cast<float>(watts[2]));
      break;
    case HP_DEV_EMMA:
      id(emma_phase_a_power).publish_state(static_cast<float>(watts[0]));
      id(emma_phase_b_power).publish_state(static_cast<float>(watts[1]));
      id(emma_phase_c_power).publish_state(static_cast<float>(watts[2]));
      break;
    default:
      break;
  }
}

static inline void hp_publish_grid_freq_and_phase_v(uint8_t device_id, const uint16_t *values) {
  if (device_id != HP_DEV_METER)
    return;
  id(meter_grid_frequency).publish_state(static_cast<float>(values[0]) * HP_GRID_FREQUENCY_SCALE);
  id(meter_phase_a_voltage).publish_state(static_cast<float>(values[1]) * HP_PHASE_VOLTAGE_SCALE);
  id(meter_phase_b_voltage).publish_state(static_cast<float>(values[2]) * HP_PHASE_VOLTAGE_SCALE);
  id(meter_phase_c_voltage).publish_state(static_cast<float>(values[3]) * HP_PHASE_VOLTAGE_SCALE);
}

static inline void hp_try_publish_count_tag(uint8_t device_id, uint16_t tag, const std::vector<uint8_t> &bytes,
                                            size_t pos) {
  const uint16_t count = hp_u16be(bytes[pos + 2], bytes[pos + 3]);

  if (tag == HP_TAG_GRID_FREQ_PHASE_V && count == 4) {
    uint16_t values[4]{};
    for (uint16_t n = 0; n < 4; n++) {
      values[n] = hp_u16be(bytes[pos + 4 + (2 * n)], bytes[pos + 5 + (2 * n)]);
    }
    hp_publish_grid_freq_and_phase_v(device_id, values);
    return;
  }

  if (count != 3)
    return;

  uint16_t values[3]{};
  for (uint16_t n = 0; n < 3; n++) {
    values[n] = hp_u16be(bytes[pos + 4 + (2 * n)], bytes[pos + 5 + (2 * n)]);
  }

  if (tag == HP_TAG_LINE_VOLTAGE) {
    hp_publish_line_voltage_sensors(device_id, values);
  } else if (tag == HP_TAG_PHASE_VOLTAGE || tag == HP_TAG_EMMA_PHASE_VOLTAGE || tag == HP_TAG_PHASE_VOLTAGE_COUNT) {
    hp_publish_phase_voltage_sensors(device_id, values);
  }
}

static inline void hp_try_publish_phase_sensors(uint8_t device_id, const std::vector<uint8_t> &bytes,
                                                size_t payload_start, size_t payload_end) {
  uint16_t line_v[3]{};
  uint16_t phase_v[3]{};
  uint16_t grid_phase_v[4]{};
  const bool has_grid_phase =
      hp_find_count_u16_tag(bytes, payload_start, payload_end, HP_TAG_GRID_FREQ_PHASE_V, 4, grid_phase_v);
  const bool has_line =
      hp_find_count_u16_tag(bytes, payload_start, payload_end, HP_TAG_LINE_VOLTAGE, 3, line_v);
  const bool has_phase = hp_find_count_u16_tag(bytes, payload_start, payload_end, HP_TAG_PHASE_VOLTAGE, 3, phase_v) ||
                         hp_find_count_u16_tag(bytes, payload_start, payload_end, HP_TAG_EMMA_PHASE_VOLTAGE, 3,
                                               phase_v) ||
                         hp_find_count_u16_tag(bytes, payload_start, payload_end, HP_TAG_PHASE_VOLTAGE_COUNT, 3,
                                               phase_v);
  if (has_grid_phase)
    hp_publish_grid_freq_and_phase_v(device_id, grid_phase_v);
  if (has_line)
    hp_publish_line_voltage_sensors(device_id, line_v);
  if (has_phase)
    hp_publish_phase_voltage_sensors(device_id, phase_v);
}

static inline bool hp_device_is_emma(uint8_t device_id) { return device_id == HP_DEV_EMMA; }

static inline std::string hp_format_count_tag(uint8_t device_id, uint16_t tag, const uint16_t *values, uint16_t count) {
  std::string out;
  char buf[48];

  if (tag == HP_TAG_LINE_VOLTAGE && count == 3) {
    snprintf(buf, sizeof(buf), "tag=0x%04X line_V AB=%.1f BC=%.1f CA=%.1f", tag,
             values[0] * HP_LINE_VOLTAGE_SCALE, values[1] * HP_LINE_VOLTAGE_SCALE,
             values[2] * HP_LINE_VOLTAGE_SCALE);
    return buf;
  }
  if (tag == HP_TAG_GRID_FREQ_PHASE_V && count == 4) {
    snprintf(buf, sizeof(buf), "tag=0x%04X freq=%.2fHz A=%.1fV B=%.1fV C=%.1fV", tag,
             values[0] * HP_GRID_FREQUENCY_SCALE, values[1] * HP_PHASE_VOLTAGE_SCALE,
             values[2] * HP_PHASE_VOLTAGE_SCALE, values[3] * HP_PHASE_VOLTAGE_SCALE);
    return buf;
  }
  if (tag == HP_TAG_PHASE_VOLTAGE && count == 3) {
    snprintf(buf, sizeof(buf), "tag=0x%04X phase_V A=%.1f B=%.1f C=%.1f", tag,
             values[0] * HP_PHASE_VOLTAGE_SCALE, values[1] * HP_PHASE_VOLTAGE_SCALE,
             values[2] * HP_PHASE_VOLTAGE_SCALE);
    return buf;
  }
  if (tag == HP_TAG_EMMA_PHASE_VOLTAGE && count == 3) {
    if (hp_device_is_emma(device_id)) {
      snprintf(buf, sizeof(buf), "tag=0x%04X EMMA phase_V A=%.1f B=%.1f C=%.1f", tag,
               values[0] * HP_PHASE_VOLTAGE_SCALE, values[1] * HP_PHASE_VOLTAGE_SCALE,
               values[2] * HP_PHASE_VOLTAGE_SCALE);
    } else {
      snprintf(buf, sizeof(buf), "tag=0x%04X phase_V A=%.1f B=%.1f C=%.1f", tag,
               values[0] * HP_PHASE_VOLTAGE_SCALE, values[1] * HP_PHASE_VOLTAGE_SCALE,
               values[2] * HP_PHASE_VOLTAGE_SCALE);
    }
    return buf;
  }
  if (tag == HP_TAG_PHASE_VOLTAGE_COUNT && count == 3) {
    snprintf(buf, sizeof(buf), "tag=0x%04X phase_V A=%.1f B=%.1f C=%.1f", tag,
             values[0] * HP_PHASE_VOLTAGE_SCALE, values[1] * HP_PHASE_VOLTAGE_SCALE,
             values[2] * HP_PHASE_VOLTAGE_SCALE);
    return buf;
  }

  snprintf(buf, sizeof(buf), "tag=0x%04X count=%u [", tag, count);
  out = buf;
  for (uint16_t n = 0; n < count; n++) {
    snprintf(buf, sizeof(buf), "%s0x%04X(%u)", n ? ", " : "", values[n], values[n]);
    out += buf;
  }
  out += "]";
  return out;
}

static inline std::string hp_tlv_value_to_ascii(const std::vector<uint8_t> &bytes, size_t start, size_t len) {
  std::string out;
  out.reserve(len);
  const size_t end = std::min(bytes.size(), start + len);
  for (size_t i = start; i < end; i++) {
    const uint8_t b = bytes[i];
    if (b == 0)
      break;
    if (b >= 0x20 && b <= 0x7E)
      out += static_cast<char>(b);
  }
  return out;
}

static inline void hp_try_publish_serial_number_value(uint8_t device_id, uint16_t tag,
                                                      const std::vector<uint8_t> &bytes, size_t pos,
                                                      size_t val_size) {
  if (tag != HP_TAG_SERIAL_NUMBER)
    return;
  if (pos + 3 + val_size > bytes.size())
    return;

  const std::string serial = hp_tlv_value_to_ascii(bytes, pos + 3, val_size);
  if (serial.empty())
    return;

  switch (device_id) {
    case HP_DEV_EMMA:
      id(emma_serial_number).publish_state(serial.c_str());
      break;
    case HP_DEV_INVERTER:
      id(inverter_serial_number).publish_state(serial.c_str());
      break;
    default:
      break;
  }
}

static inline void hp_try_publish_bus_clock(uint8_t device_id, uint16_t tag, uint8_t type,
                                            const std::vector<uint8_t> &bytes, size_t pos) {
  size_t val_size = 0;
  if (!hp_tlv_type_size(type, &val_size))
    return;
  hp_try_publish_bus_clock_value(device_id, tag, bytes, pos, val_size);
}

static inline void hp_try_publish_serial_number(uint8_t device_id, uint16_t tag, uint8_t type,
                                                const std::vector<uint8_t> &bytes, size_t pos) {
  size_t val_size = 0;
  if (!hp_tlv_type_size(type, &val_size))
    return;
  hp_try_publish_serial_number_value(device_id, tag, bytes, pos, val_size);
}

static inline std::string hp_format_scalar_i32_u32(int32_t i32) {
  char buf[48];
  if (i32 < 0) {
    snprintf(buf, sizeof(buf), "i32=%ld", static_cast<long>(i32));
  } else {
    snprintf(buf, sizeof(buf), "u32=%lu", static_cast<unsigned long>(i32));
  }
  return buf;
}

static inline std::string hp_format_typed_tlv(uint8_t device_id, const std::vector<uint8_t> &bytes, size_t pos,
                                              uint16_t tag, uint8_t type, size_t val_size) {
  char line[256];
  if (type == 0x01) {
    snprintf(line, sizeof(line), "tag=0x%04X u8=%u", tag, bytes[pos + 3]);
    return line;
  }
  if (type == 0x02) {
    const int32_t i32 = hp_i32be(bytes, pos + 3);
    if (tag == HP_TAG_ACTIVE_POWER) {
      snprintf(line, sizeof(line), "tag=0x%04X P_act=%ldW", tag, static_cast<long>(i32));
      return line;
    }
    if (tag == HP_TAG_REACTIVE_POWER) {
      snprintf(line, sizeof(line), "tag=0x%04X Q=%ldvar", tag, static_cast<long>(i32));
      return line;
    }
    if (tag == HP_TAG_REACTIVE_AUX) {
      snprintf(line, sizeof(line), "tag=0x%04X Q_aux=%ld", tag, static_cast<long>(i32));
      return line;
    }
    if (tag == HP_TAG_TOTAL_YIELD && i32 >= 0) {
      snprintf(line, sizeof(line), "tag=0x%04X total_yield=%.2fkWh", tag, i32 * HP_ENERGY_YIELD_SCALE);
      return line;
    }
    if (tag == HP_TAG_DAILY_ENERGY && i32 >= 0) {
      snprintf(line, sizeof(line), "tag=0x%04X daily_energy=%.2fkWh", tag, i32 * HP_ENERGY_YIELD_SCALE);
      return line;
    }
    if (tag == HP_TAG_BATTERY_CHARGED_TODAY && i32 >= 0) {
      snprintf(line, sizeof(line), "tag=0x%04X battery_charged=%.2fkWh", tag, i32 * HP_ENERGY_YIELD_SCALE);
      return line;
    }
    if (tag == HP_TAG_BATTERY_DISCHARGED_TODAY && i32 >= 0) {
      snprintf(line, sizeof(line), "tag=0x%04X battery_discharged=%.2fkWh", tag, i32 * HP_ENERGY_YIELD_SCALE);
      return line;
    }
    if (tag == HP_TAG_BATTERY_TOTAL_CHARGE && i32 >= 0) {
      snprintf(line, sizeof(line), "tag=0x%04X battery_total_charge=%.2fkWh", tag, i32 * HP_ENERGY_YIELD_SCALE);
      return line;
    }
    if (tag == HP_TAG_BATTERY_POWER) {
      snprintf(line, sizeof(line), "tag=0x%04X battery_P=%ldW", tag, static_cast<long>(i32));
      return line;
    }
    if (tag == HP_TAG_EMMA_ACTIVE_POWER_W) {
      if (hp_device_is_emma(device_id)) {
        snprintf(line, sizeof(line), "tag=0x%04X EMMA_P=%ldW", tag, static_cast<long>(i32));
      } else {
        snprintf(line, sizeof(line), "tag=0x%04X P_act=%ldW", tag, static_cast<long>(i32));
      }
      return line;
    }
    const std::string scalar = hp_format_scalar_i32_u32(i32);
    snprintf(line, sizeof(line), "tag=0x%04X %s", tag, scalar.c_str());
    return line;
  }
  if (type == 0x03) {
    const int16_t v1 = hp_i16be(bytes[pos + 3], bytes[pos + 4]);
    const int16_t v2 = hp_i16be(bytes[pos + 5], bytes[pos + 6]);
    const int16_t v3 = hp_i16be(bytes[pos + 7], bytes[pos + 8]);
    if (tag == HP_TAG_EMMA_PHASE_ACTIVE_POWER) {
      if (hp_device_is_emma(device_id)) {
        snprintf(line, sizeof(line), "tag=0x%04X EMMA P A=%dW B=%dW C=%dW", tag, v1, v2, v3);
      } else {
        snprintf(line, sizeof(line), "tag=0x%04X P A=%dW B=%dW C=%dW", tag, v1, v2, v3);
      }
      return line;
    }
    snprintf(line, sizeof(line), "tag=0x%04X i16[3]=%d,%d,%d", tag, v1, v2, v3);
    return line;
  }
  if (type == 0x04) {
    const int32_t v1 = hp_i32be(bytes, pos + 3);
    const int32_t v2 = hp_i32be(bytes, pos + 7);
    if (tag == HP_TAG_INVERTER_SOLAR_PV) {
      snprintf(line, sizeof(line), "tag=0x%04X PV1=%ldW PV2=%ldW", tag, static_cast<long>(v1), static_cast<long>(v2));
      return line;
    }
    snprintf(line, sizeof(line), "tag=0x%04X i32[2]=%ld,%ld", tag, static_cast<long>(v1), static_cast<long>(v2));
    return line;
  }
  if (type == 0x08) {
    snprintf(line, sizeof(line), "tag=0x%04X raw8=%s", tag, hp_hex_slice(bytes, pos + 3, 8).c_str());
    return line;
  }
  if (type == 0x06) {
    const int32_t v1 = hp_i32be(bytes, pos + 3);
    const int32_t v2 = hp_i32be(bytes, pos + 7);
    const int32_t v3 = hp_i32be(bytes, pos + 11);
    if (tag == HP_TAG_PHASE_CURRENT) {
      snprintf(line, sizeof(line), "tag=0x%04X I A=%.3fA B=%.3fA C=%.3fA", tag, v1 * HP_PHASE_CURRENT_SCALE,
               v2 * HP_PHASE_CURRENT_SCALE, v3 * HP_PHASE_CURRENT_SCALE);
      return line;
    }
    if (tag == HP_TAG_PHASE_ACTIVE_POWER) {
      snprintf(line, sizeof(line), "tag=0x%04X P A=%ldW B=%ldW C=%ldW", tag, static_cast<long>(v1),
               static_cast<long>(v2), static_cast<long>(v3));
      return line;
    }
    if (tag == HP_TAG_EMMA_PHASE_ACTIVE_POWER_I32) {
      if (hp_device_is_emma(device_id)) {
        snprintf(line, sizeof(line), "tag=0x%04X EMMA P A=%ldW B=%ldW C=%ldW", tag, static_cast<long>(v1),
                 static_cast<long>(v2), static_cast<long>(v3));
      } else {
        snprintf(line, sizeof(line), "tag=0x%04X P A=%ldW B=%ldW C=%ldW", tag, static_cast<long>(v1),
                 static_cast<long>(v2), static_cast<long>(v3));
      }
      return line;
    }
    if (tag == HP_TAG_PHASE_VOLTAGE_U32) {
      snprintf(line, sizeof(line), "tag=0x%04X phase_V A=%.1f B=%.1f C=%.1f", tag, v1 * HP_PHASE_VOLTAGE_SCALE,
               v2 * HP_PHASE_VOLTAGE_SCALE, v3 * HP_PHASE_VOLTAGE_SCALE);
      return line;
    }
    if (tag == HP_TAG_INVERTER_TIMES) {
      char t0[32];
      char t1[32];
      char t2[32];
      hp_format_fc41_local_time(static_cast<uint32_t>(v1), t0, sizeof(t0));
      hp_format_fc41_local_time(static_cast<uint32_t>(v2), t1, sizeof(t1));
      hp_format_fc41_local_time(static_cast<uint32_t>(v3), t2, sizeof(t2));
      snprintf(line, sizeof(line), "tag=0x%04X startup=%s shutdown=%s field3=%s", tag, t0, t1, t2);
      return line;
    }
    snprintf(line, sizeof(line), "tag=0x%04X i32[3]=%ld,%ld,%ld", tag, static_cast<long>(v1), static_cast<long>(v2),
             static_cast<long>(v3));
    return line;
  }
  if (type == 0x0A) {
    if (tag == HP_TAG_BATTERY_TOTAL_DISCHARGE && pos + 3 + 9 <= bytes.size()) {
      const uint32_t charge_raw = hp_u32be(bytes, pos + 3);
      const uint32_t discharge_raw = hp_u32be(bytes, pos + 3 + HP_BATTERY_TOTAL_DISCHARGE_OPAQUE_OFFSET);
      snprintf(line, sizeof(line), "tag=0x%04X battery_total_charge=%.2fkWh battery_total_discharge=%.2fkWh", tag,
               charge_raw * HP_ENERGY_YIELD_SCALE, discharge_raw * HP_ENERGY_YIELD_SCALE);
      return line;
    }
    if (tag == HP_TAG_SERIAL_NUMBER) {
      const std::string serial = hp_tlv_value_to_ascii(bytes, pos + 3, val_size);
      snprintf(line, sizeof(line), "tag=0x%04X serial=%s", tag, serial.c_str());
      return line;
    }
    snprintf(line, sizeof(line), "tag=0x%04X opaque=%s", tag, hp_hex_slice(bytes, pos + 3, 10).c_str());
    return line;
  }
  if (type == 0x0C) {
    const uint32_t u1 = hp_u32be(bytes, pos + 3);
    const uint32_t u2 = hp_u32be(bytes, pos + 7);
    const uint32_t u3 = hp_u32be(bytes, pos + 11);
    if (tag == HP_TAG_PHASE_VOLTAGE_U32) {
      snprintf(line, sizeof(line), "tag=0x%04X phase_V A=%.1f B=%.1f C=%.1f", tag, u1 * HP_PHASE_VOLTAGE_SCALE,
               u2 * HP_PHASE_VOLTAGE_SCALE, u3 * HP_PHASE_VOLTAGE_SCALE);
      return line;
    }
    if (tag == HP_TAG_LINE_VOLTAGE_U32) {
      snprintf(line, sizeof(line), "tag=0x%04X line_V AB=%.1f BC=%.1f CA=%.1f", tag, u1 * HP_LINE_VOLTAGE_SCALE,
               u2 * HP_LINE_VOLTAGE_SCALE, u3 * HP_LINE_VOLTAGE_SCALE);
      return line;
    }
    snprintf(line, sizeof(line), "tag=0x%04X u32[3]=%lu,%lu,%lu", tag, static_cast<unsigned long>(u1),
             static_cast<unsigned long>(u2), static_cast<unsigned long>(u3));
    return line;
  }

  snprintf(line, sizeof(line), "tag=0x%04X type=0x%02X raw=%s", tag, type,
           hp_hex_slice(bytes, pos + 3, val_size).c_str());
  return line;
}

// Sub-function 0x35 uses a word count, not a value type. Preserve the useful
// tag-specific interpretations where their sizes match, and render every other
// value losslessly using the full count * 2-byte payload.
static inline std::string hp_format_current_data_tlv(uint8_t device_id, const std::vector<uint8_t> &bytes,
                                                     size_t pos, uint16_t tag, uint8_t word_count,
                                                     size_t val_size) {
  char line[320];
  if (word_count == 1) {
    const uint16_t word = hp_u16be(bytes[pos + 3], bytes[pos + 4]);
    snprintf(line, sizeof(line), "tag=0x%04X words=1 value=0x%04X(%u)", tag, word, word);
    return line;
  }

  if (word_count == 2 || word_count == 3 || word_count == 4 || word_count == 6)
    return hp_format_typed_tlv(device_id, bytes, pos, tag, word_count, val_size);

  if ((word_count == 10 && (tag == HP_TAG_BATTERY_TOTAL_DISCHARGE || tag == HP_TAG_SERIAL_NUMBER)) ||
      (word_count == 12 && (tag == HP_TAG_PHASE_VOLTAGE_U32 || tag == HP_TAG_LINE_VOLTAGE_U32))) {
    const std::string decoded = hp_format_typed_tlv(device_id, bytes, pos, tag, word_count, val_size);
    snprintf(line, sizeof(line), "%s raw=%s", decoded.c_str(), hp_hex_slice(bytes, pos + 3, val_size).c_str());
    return line;
  }

  snprintf(line, sizeof(line), "tag=0x%04X words=%u raw=%s", tag, word_count,
           hp_hex_slice(bytes, pos + 3, val_size).c_str());
  return line;
}

static inline bool hp_try_parse_count_tlv(uint8_t device_id, const std::vector<uint8_t> &bytes, size_t pos, size_t end,
                                          std::string &out, size_t &advance) {
  if (pos + 4 > end)
    return false;

  const uint16_t tag = hp_u16be(bytes[pos], bytes[pos + 1]);
  if (!hp_is_count_u16_tag(tag))
    return false;

  const uint16_t count = hp_u16be(bytes[pos + 2], bytes[pos + 3]);
  if (count == 0 || count > 16 || pos + 4 + (2 * count) > end)
    return false;

  uint16_t values[16]{};
  for (uint16_t n = 0; n < count; n++) {
    values[n] = hp_u16be(bytes[pos + 4 + (2 * n)], bytes[pos + 5 + (2 * n)]);
  }

  out = hp_format_count_tag(device_id, tag, values, count);
  advance = 4 + (2 * count);
  return true;
}

static inline bool hp_try_parse_typed_tlv(uint8_t device_id, const std::vector<uint8_t> &bytes, size_t pos, size_t end,
                                          std::string &out, size_t &advance) {
  if (pos + 3 > end)
    return false;

  const uint16_t tag = hp_u16be(bytes[pos], bytes[pos + 1]);
  const uint8_t type = bytes[pos + 2];
  if (!hp_tlv_tag_looks_valid(tag))
    return false;
  size_t val_size = 0;
  if (!hp_tlv_type_size(type, &val_size))
    return false;
  if (pos + 3 + val_size > end)
    return false;

  out = hp_format_typed_tlv(device_id, bytes, pos, tag, type, val_size);
  advance = 3 + val_size;
  return true;
}

static inline void hp_update_power_factor(uint8_t device_id, bool update_p, float p_watts, bool update_q,
                                          float q_var) {
  HpPfState *state = nullptr;
  switch (device_id) {
    case HP_DEV_EMMA:
      state = &hp_pf_emma;
      break;
    case HP_DEV_INVERTER:
      state = &hp_pf_inverter;
      break;
    default:
      return;
  }

  if (update_p) {
    state->p_watts = p_watts;
    state->have_p = true;
  }
  if (update_q) {
    state->q_var = q_var;
    state->have_q = true;
  }
  if (!state->have_p || !state->have_q || std::fabs(state->p_watts) < 1.0f)
    return;

  const float s = std::sqrt(state->p_watts * state->p_watts + state->q_var * state->q_var);
  if (s < 1.0f)
    return;

  const float pf = state->p_watts / s;
  if (device_id == HP_DEV_EMMA) {
    id(emma_power_factor).publish_state(pf);
  } else if (device_id == HP_DEV_INVERTER) {
    id(inverter_power_factor).publish_state(pf);
  }
}

static inline void hp_try_publish_phase_from_typed_tlv(uint8_t device_id, uint16_t tag, uint8_t type,
                                                       const std::vector<uint8_t> &bytes, size_t pos) {
  if (tag == HP_TAG_BATTERY_TOTAL_DISCHARGE && type == 0x0A) {
    if (device_id != HP_DEV_INVERTER || pos + 3 + 9 > bytes.size())
      return;
    const uint32_t discharge_raw = hp_u32be(bytes, pos + 3 + HP_BATTERY_TOTAL_DISCHARGE_OPAQUE_OFFSET);
    id(inverter_battery_total_discharge).publish_state(static_cast<float>(discharge_raw) * HP_ENERGY_YIELD_SCALE);
    return;
  }

  if (tag == HP_TAG_INVERTER_SOLAR_PV && type == 0x04) {
    if (device_id != HP_DEV_INVERTER)
      return;
    id(inverter_solar_power_pv1).publish_state(static_cast<float>(hp_i32be(bytes, pos + 3)));
    id(inverter_solar_power_pv2).publish_state(static_cast<float>(hp_i32be(bytes, pos + 7)));
    return;
  }

  if (type != 0x02 && type != 0x03 && type != 0x04 && type != 0x06 && type != 0x0C)
    return;

  if (tag == HP_TAG_ACTIVE_POWER && type == 0x02) {
    const float watts = static_cast<float>(hp_i32be(bytes, pos + 3));
    switch (device_id) {
      case HP_DEV_EMMA:
        id(emma_active_power).publish_state(watts);
        hp_update_power_factor(device_id, true, watts, false, 0.0f);
        break;
      case HP_DEV_INVERTER:
        id(inverter_active_power).publish_state(watts);
        hp_update_power_factor(device_id, true, watts, false, 0.0f);
        break;
      default:
        break;
    }
    return;
  }
  if (tag == HP_TAG_REACTIVE_POWER && type == 0x02) {
    const float q_var = static_cast<float>(hp_i32be(bytes, pos + 3));
    switch (device_id) {
      case HP_DEV_EMMA:
        id(emma_reactive_power).publish_state(q_var);
        hp_update_power_factor(device_id, false, 0.0f, true, q_var);
        break;
      case HP_DEV_INVERTER:
        id(inverter_reactive_power).publish_state(q_var);
        hp_update_power_factor(device_id, false, 0.0f, true, q_var);
        break;
      default:
        break;
    }
    return;
  }
  if (tag == HP_TAG_REACTIVE_AUX && type == 0x02) {
    if (device_id != HP_DEV_INVERTER)
      return;
    const float q_var = static_cast<float>(hp_i32be(bytes, pos + 3));
    id(inverter_reactive_power_aux).publish_state(q_var);
    return;
  }
  if (tag == HP_TAG_TOTAL_YIELD && type == 0x02) {
    const uint32_t raw = hp_u32be(bytes, pos + 3);
    hp_publish_total_yield(device_id, raw);
    return;
  }
  if (tag == HP_TAG_DAILY_ENERGY && type == 0x02) {
    const uint32_t raw = hp_u32be(bytes, pos + 3);
    hp_publish_daily_energy(device_id, raw);
    return;
  }
  if (tag == HP_TAG_BATTERY_CHARGED_TODAY && type == 0x02) {
    if (device_id != HP_DEV_INVERTER)
      return;
    const float kwh = static_cast<float>(hp_u32be(bytes, pos + 3)) * HP_ENERGY_YIELD_SCALE;
    id(inverter_battery_charged_today).publish_state(kwh);
    return;
  }
  if (tag == HP_TAG_BATTERY_DISCHARGED_TODAY && type == 0x02) {
    if (device_id != HP_DEV_INVERTER)
      return;
    const float kwh = static_cast<float>(hp_u32be(bytes, pos + 3)) * HP_ENERGY_YIELD_SCALE;
    id(inverter_battery_discharged_today).publish_state(kwh);
    return;
  }
  if (tag == HP_TAG_BATTERY_POWER && type == 0x02) {
    if (device_id != HP_DEV_INVERTER)
      return;
    id(inverter_battery_power).publish_state(static_cast<float>(hp_i32be(bytes, pos + 3)));
    return;
  }
  if (tag == HP_TAG_BATTERY_TOTAL_CHARGE && type == 0x02) {
    if (device_id != HP_DEV_INVERTER)
      return;
    const float kwh = static_cast<float>(hp_u32be(bytes, pos + 3)) * HP_ENERGY_YIELD_SCALE;
    id(inverter_battery_total_charge).publish_state(kwh);
    return;
  }
  if (tag == HP_TAG_EMMA_ACTIVE_POWER_W && type == 0x02) {
    if (device_id != HP_DEV_EMMA)
      return;
    const int32_t watts = hp_i32be(bytes, pos + 3);
    id(emma_active_power).publish_state(static_cast<float>(watts));
    hp_update_power_factor(device_id, true, static_cast<float>(watts), false, 0.0f);
    return;
  }
  if (tag == HP_TAG_PHASE_CURRENT && type == 0x06) {
    const int32_t currents[3] = {hp_i32be(bytes, pos + 3), hp_i32be(bytes, pos + 7), hp_i32be(bytes, pos + 11)};
    hp_publish_phase_current_sensors(device_id, currents);
    return;
  }
  if (tag == HP_TAG_PHASE_ACTIVE_POWER && type == 0x06) {
    const int32_t watts[3] = {hp_i32be(bytes, pos + 3), hp_i32be(bytes, pos + 7), hp_i32be(bytes, pos + 11)};
    hp_publish_phase_active_power_sensors(device_id, watts);
    return;
  }
  if (tag == HP_TAG_EMMA_PHASE_ACTIVE_POWER_I32 && type == 0x06) {
    if (device_id != HP_DEV_EMMA)
      return;
    const int32_t watts[3] = {hp_i32be(bytes, pos + 3), hp_i32be(bytes, pos + 7), hp_i32be(bytes, pos + 11)};
    hp_publish_phase_active_power_sensors(device_id, watts);
    return;
  }
  if (tag == HP_TAG_INVERTER_TIMES && type == 0x06) {
    const uint32_t startup = hp_u32be(bytes, pos + 3);
    const uint32_t shutdown = hp_u32be(bytes, pos + 7);
    const uint32_t field3 = hp_u32be(bytes, pos + 11);
    hp_publish_inverter_times(device_id, startup, shutdown, field3);
    return;
  }

  uint16_t voltages[3]{};
  if (tag == HP_TAG_EMMA_PHASE_ACTIVE_POWER && type == 0x03) {
    const int32_t watts[3] = {hp_i16be(bytes[pos + 3], bytes[pos + 4]), hp_i16be(bytes[pos + 5], bytes[pos + 6]),
                              hp_i16be(bytes[pos + 7], bytes[pos + 8])};
    hp_publish_phase_active_power_sensors(device_id, watts);
    return;
  }
  if (tag == HP_TAG_PHASE_VOLTAGE_TLV && type == 0x03) {
    voltages[0] = static_cast<uint16_t>(hp_i16be(bytes[pos + 3], bytes[pos + 4]));
    voltages[1] = static_cast<uint16_t>(hp_i16be(bytes[pos + 5], bytes[pos + 6]));
    voltages[2] = static_cast<uint16_t>(hp_i16be(bytes[pos + 7], bytes[pos + 8]));
    hp_publish_phase_voltage_sensors(device_id, voltages);
    return;
  }
  if (tag == HP_TAG_PHASE_VOLTAGE_U32 && (type == 0x0C || type == 0x06)) {
    const uint32_t u1 = hp_u32be(bytes, pos + 3);
    const uint32_t u2 = hp_u32be(bytes, pos + 7);
    const uint32_t u3 = hp_u32be(bytes, pos + 11);
    voltages[0] = static_cast<uint16_t>(u1 & 0xFFFF);
    voltages[1] = static_cast<uint16_t>(u2 & 0xFFFF);
    voltages[2] = static_cast<uint16_t>(u3 & 0xFFFF);
    hp_publish_phase_voltage_sensors(device_id, voltages);
    return;
  }
  if (tag == HP_TAG_LINE_VOLTAGE_U32 && type == 0x0C) {
    const uint32_t u1 = hp_u32be(bytes, pos + 3);
    const uint32_t u2 = hp_u32be(bytes, pos + 7);
    const uint32_t u3 = hp_u32be(bytes, pos + 11);
    const uint16_t line_v[3] = {static_cast<uint16_t>(u1), static_cast<uint16_t>(u2), static_cast<uint16_t>(u3)};
    hp_publish_line_voltage_sensors(device_id, line_v);
  }
}

static inline void hp_walk_current_data_tlvs(const std::vector<uint8_t> &bytes, size_t start, size_t end,
                                             uint8_t expected_count, uint8_t device_id,
                                             uint8_t &parsed_count, size_t &consumed_end,
                                             std::vector<std::string> &entries) {
  size_t pos = start;
  parsed_count = 0;

  while (parsed_count < expected_count) {
    if (pos + 3 > end)
      break;

    const uint16_t tag = hp_u16be(bytes[pos], bytes[pos + 1]);
    const uint8_t word_count = bytes[pos + 2];
    const size_t val_size = static_cast<size_t>(word_count) * 2;
    if (pos + 3 + val_size > end)
      break;

    const std::string entry = hp_format_current_data_tlv(device_id, bytes, pos, tag, word_count, val_size);
    entries.push_back(entry);

    hp_try_publish_bus_clock_value(device_id, tag, bytes, pos, val_size);
    hp_try_publish_serial_number_value(device_id, tag, bytes, pos, val_size);
    hp_try_publish_phase_from_typed_tlv(device_id, tag, word_count, bytes, pos);

    pos += 3 + val_size;
    parsed_count++;
  }

  consumed_end = pos;
}

static inline std::string hp_walk_tlv_payload(const std::vector<uint8_t> &bytes, size_t start, size_t end,
                                              bool publish_phase, uint8_t device_id, bool resilient_scan = false) {
  std::string tags;
  size_t pos = start;
  const size_t max_steps = (end > start) ? (end - start) + 4 : 0;
  size_t steps = 0;

  while (pos + 3 <= end && steps++ < max_steps) {
    std::string entry;
    size_t advance = 0;
    const bool is_typed = hp_try_parse_typed_tlv(device_id, bytes, pos, end, entry, advance);

    if (!is_typed && !hp_try_parse_count_tlv(device_id, bytes, pos, end, entry, advance)) {
      if (resilient_scan) {
        pos++;
        continue;
      }
      break;
    }

    const uint16_t tag = hp_u16be(bytes[pos], bytes[pos + 1]);
    if (is_typed && bytes[pos + 2] == 0x0A) {
      hp_try_publish_bus_clock(device_id, tag, 0x0A, bytes, pos);
    }
    if (is_typed)
      hp_try_publish_serial_number(device_id, tag, bytes[pos + 2], bytes, pos);

    if (publish_phase) {
      if (is_typed) {
        hp_try_publish_phase_from_typed_tlv(device_id, tag, bytes[pos + 2], bytes, pos);
      } else if (tag == HP_TAG_LINE_VOLTAGE || tag == HP_TAG_PHASE_VOLTAGE || tag == HP_TAG_GRID_FREQ_PHASE_V ||
                 tag == HP_TAG_EMMA_PHASE_VOLTAGE || tag == HP_TAG_PHASE_VOLTAGE_COUNT) {
        hp_try_publish_count_tag(device_id, tag, bytes, pos);
      }
    }

    if (!tags.empty())
      tags += " | ";
    tags += entry;
    pos += advance;
  }

  return tags;
}

static inline bool hp_0x41_frame_bounds(const std::vector<uint8_t> &bytes, size_t p, size_t &payload_start,
                                        size_t &payload_end, size_t &frame_end) {
  if (p + 4 > bytes.size() || bytes[p + 1] != 0x41)
    return false;

  const uint8_t subfunction = bytes[p + 2];

  if (subfunction == HP_FC41_SUBFN_CURRENT_DATA) {
    const uint8_t len = bytes[p + 3];
    payload_start = p + 4;
    payload_end = payload_start + len;
    frame_end = payload_end + 2;
    return frame_end <= bytes.size();
  }

  if (subfunction == HP_FC41_SUBFN_REPORT) {
    if (p + 5 > bytes.size())
      return false;
    const uint16_t len = hp_u16be(bytes[p + 3], bytes[p + 4]);
    payload_start = p + 5;
    payload_end = payload_start + len;
    frame_end = payload_end + 2;
    return frame_end <= bytes.size();
  }

  if (hp_is_count_u16_tag(hp_u16be(subfunction, bytes[p + 3]))) {
    payload_start = p + 2;
    if (payload_start + 4 > bytes.size())
      return false;
    const uint16_t count = hp_u16be(bytes[payload_start + 2], bytes[payload_start + 3]);
    if (count == 0 || count > 16)
      return false;
    payload_end = payload_start + 4 + (2 * count);
    frame_end = payload_end + 2;
    return frame_end <= bytes.size();
  }

  return false;
}

static inline size_t hp_find_tlv_start(const std::vector<uint8_t> &bytes, size_t start, size_t end) {
  const size_t scan_limit = std::min(end, start + 24);
  for (size_t k = start; k + 4 < scan_limit; k++) {
    const uint16_t tag = hp_u16be(bytes[k], bytes[k + 1]);
    if (!hp_is_count_u16_tag(tag))
      continue;
    const uint16_t count = hp_u16be(bytes[k + 2], bytes[k + 3]);
    if (count == 0 || count > 16 || k + 4 + (2 * count) > end)
      continue;
    return k;
  }
  for (size_t k = start; k + 3 < scan_limit; k++) {
    std::string dummy;
    size_t advance = 0;
    if (hp_try_parse_typed_tlv(0xFF, bytes, k, end, dummy, advance))
      return k;
  }
  return start;
}

static inline std::string hp_decode_0x35_tlv(const std::vector<uint8_t> &bytes, size_t p, size_t payload_start,
                                             size_t payload_end, std::vector<std::string> &entries) {
  const uint8_t device_id = bytes[p];
  const uint8_t len = bytes[p + 3];

  char head[160];
  snprintf(head, sizeof(head), "dev=%u (0x%02X) fc=0x41 subfn=0x35 (current-sensor-data) len=%u", device_id,
           device_id, len);
  std::string summary = head;

  if (payload_end <= payload_start) {
    summary += " :: (empty)";
    return summary;
  }

  if (payload_end - payload_start == 1) {
    char single[24];
    snprintf(single, sizeof(single), " :: heartbeat=0x%02X", bytes[payload_start]);
    summary += single;
    return summary;
  }

  if (payload_end - payload_start < 2) {
    summary += " :: malformed: missing sequence/TLV count";
    return summary;
  }

  const uint8_t sequence = bytes[payload_start];
  const uint8_t expected_count = bytes[payload_start + 1];
  uint8_t parsed_count = 0;
  size_t consumed_end = payload_start + 2;
  hp_walk_current_data_tlvs(bytes, payload_start + 2, payload_end, expected_count, device_id, parsed_count,
                            consumed_end, entries);

  char meta[64];
  snprintf(meta, sizeof(meta), " seq=0x%02X tlvs=%u", sequence, expected_count);
  summary += meta;

  if (parsed_count != expected_count || consumed_end != payload_end) {
    char error[96];
    snprintf(error, sizeof(error), " :: parse-error parsed=%u/%u consumed=%zu/%zu", parsed_count, expected_count,
             consumed_end - payload_start, payload_end - payload_start);
    summary += error;
  }

  return summary;
}

static inline std::string hp_decode_0x89_report(const std::vector<uint8_t> &bytes, size_t p, size_t payload_start,
                                                size_t payload_end) {
  const uint8_t device_id = bytes[p];
  const uint16_t len = hp_u16be(bytes[p + 3], bytes[p + 4]);

  char head[128];
  snprintf(head, sizeof(head), "dev=%u (0x%02X) fc=0x41 subfn=0x89 (report) len=%u", device_id, device_id, len);
  std::string summary = head;

  const size_t tlv_start = hp_find_tlv_start(bytes, payload_start, payload_end);
  if (tlv_start > payload_start) {
    char skip[16];
    snprintf(skip, sizeof(skip), " skip=%zu", tlv_start - payload_start);
    summary += skip;
  }

  const std::string tags = hp_walk_tlv_payload(bytes, tlv_start, payload_end, true, device_id);
  if (!tags.empty()) {
    summary += " :: ";
    summary += tags;
  } else {
    hp_try_publish_phase_sensors(device_id, bytes, payload_start, payload_end);
  }

  return summary;
}

static inline std::string hp_decode_direct_tag_frame(const std::vector<uint8_t> &bytes, size_t p, size_t payload_start,
                                                     size_t payload_end) {
  const uint8_t device_id = bytes[p];
  const uint16_t tag = hp_u16be(bytes[p + 2], bytes[p + 3]);

  char head[96];
  snprintf(head, sizeof(head), "dev=%u (0x%02X) fc=0x41 direct-tag=0x%04X", device_id, device_id, tag);
  std::string summary = head;

  const std::string tags = hp_walk_tlv_payload(bytes, payload_start, payload_end, true, device_id);
  if (!tags.empty()) {
    summary += " :: ";
    summary += tags;
  }

  return summary;
}

static inline std::string hp_decode_0x41_frame(const std::vector<uint8_t> &bytes, size_t p, size_t frame_end) {
  size_t payload_start = 0;
  size_t payload_end = 0;
  size_t bounded_end = frame_end;

  if (!hp_0x41_frame_bounds(bytes, p, payload_start, payload_end, bounded_end))
    return {};

  const uint8_t subfunction = bytes[p + 2];
  std::string summary;
  std::vector<std::string> current_entries;

  if (subfunction == HP_FC41_SUBFN_CURRENT_DATA) {
    summary = hp_decode_0x35_tlv(bytes, p, payload_start, payload_end, current_entries);
  } else if (subfunction == HP_FC41_SUBFN_REPORT) {
    summary = hp_decode_0x89_report(bytes, p, payload_start, payload_end);
  } else if (hp_is_count_u16_tag(hp_u16be(subfunction, bytes[p + 3]))) {
    summary = hp_decode_direct_tag_frame(bytes, p, payload_start, payload_end);
  } else {
    return {};
  }

  ESP_LOGI(HUAWEI_PROP_TAG, "FC41 %s", summary.c_str());
  const unsigned expected_entries =
      (subfunction == HP_FC41_SUBFN_CURRENT_DATA && payload_end >= payload_start + 2)
          ? static_cast<unsigned>(bytes[payload_start + 1])
          : static_cast<unsigned>(current_entries.size());
  for (size_t i = 0; i < current_entries.size(); i++) {
    ESP_LOGI(HUAWEI_PROP_TAG, "  TLV %u/%u %s", static_cast<unsigned>(i + 1),
             expected_entries, current_entries[i].c_str());
  }
  if (subfunction == HP_FC41_SUBFN_CURRENT_DATA || subfunction == HP_FC41_SUBFN_REPORT) {
    hp_log_hex_chunks(bytes, p, bounded_end - p, "raw-frame");
  } else {
    ESP_LOGD(HUAWEI_PROP_TAG, "  raw=%s", hp_hex_slice(bytes, p, bounded_end - p).c_str());
  }

  return summary;
}

static inline void hp_log_opaque_upload_buffer(const std::vector<uint8_t> &bytes) {
  const uint8_t device_id = bytes[0];
  const uint8_t subfunction = bytes[2];
  const bool crc_ok = hp_modbus_crc_matches(bytes, 0, bytes.size());
  ESP_LOGI(HUAWEI_PROP_TAG,
           "FC41 dev=%u (0x%02X) fc=0x41 subfn=0x%02X (%s) opaque-upload bytes=%u crc=%s",
           device_id, device_id, subfunction, hp_subfunction_name(subfunction),
           static_cast<unsigned>(bytes.size()), crc_ok ? "ok" : "unverified");
  hp_log_hex_chunks(bytes, 0, bytes.size(), "upload-raw");
  hp_note_device_id(device_id);
}

static inline void hp_log_exception_buffer(const std::vector<uint8_t> &bytes) {
  const uint8_t device_id = bytes[0];
  const uint8_t exception = bytes[2];
  const bool crc_ok = hp_modbus_crc_matches(bytes, 0, bytes.size());
  ESP_LOGW(HUAWEI_PROP_TAG,
           "FC41 dev=%u (0x%02X) function=0xC1 (exception) code=0x%02X (%s) bytes=%u crc=%s",
           device_id, device_id, exception, hp_modbus_exception_name(exception),
           static_cast<unsigned>(bytes.size()), crc_ok ? "ok" : "unverified");
  hp_log_hex_chunks(bytes, 0, bytes.size(), "exception-raw");
  hp_note_device_id(device_id);
}

// Passive UART sniff decoder for Huawei proprietary Modbus function 0x41 (EMMA / inverter traffic).
static inline void huawei_decode_proprietary_uart_buffer(const std::vector<uint8_t> &bytes) {
  if (bytes.size() < 3)
    return;

  // Request and response upload PDUs share the same function/sub-function pair.
  // Keep the complete UART burst opaque because this RX-only sniffer cannot
  // reliably determine direction without decoding the upload-specific body.
  if (bytes[1] == 0x41 && hp_is_upload_subfunction(bytes[2])) {
    hp_log_opaque_upload_buffer(bytes);
    hp_publish_discovered_device_ids();
    return;
  }

  // Standard Modbus exception response for proprietary function 0x41.
  if (bytes[1] == HP_FC41_EXCEPTION_FUNCTION) {
    hp_log_exception_buffer(bytes);
    hp_publish_discovered_device_ids();
    return;
  }

  bool found = false;

  for (size_t p = 0; p + 3 < bytes.size(); p++) {
    if (bytes[p + 1] != 0x41)
      continue;

    const uint8_t subfunction = bytes[p + 2];
    const bool direct_tag = hp_is_count_u16_tag(hp_u16be(subfunction, bytes[p + 3]));
    if (subfunction != HP_FC41_SUBFN_CURRENT_DATA && subfunction != HP_FC41_SUBFN_REPORT && !direct_tag)
      continue;

    size_t payload_start = 0;
    size_t payload_end = 0;
    size_t frame_end = 0;
    if (!hp_0x41_frame_bounds(bytes, p, payload_start, payload_end, frame_end)) {
      if (subfunction == HP_FC41_SUBFN_CURRENT_DATA || subfunction == HP_FC41_SUBFN_REPORT) {
        ESP_LOGW(HUAWEI_PROP_TAG, "FC41 truncated dev=%u (0x%02X) subfn=0x%02X (%s) bytes=%u", bytes[p], bytes[p],
                 subfunction, hp_subfunction_name(subfunction), static_cast<unsigned>(bytes.size() - p));
        hp_log_hex_chunks(bytes, p, bytes.size() - p, "truncated-raw");
      }
      continue;
    }

    if (!hp_modbus_crc_matches(bytes, p, frame_end)) {
      ESP_LOGW(HUAWEI_PROP_TAG, "FC41 CRC mismatch dev=%u (0x%02X) subfn=0x%02X (%s)", bytes[p], bytes[p],
               subfunction, hp_subfunction_name(subfunction));
      continue;
    }

    hp_note_device_id(bytes[p]);
    hp_decode_0x41_frame(bytes, p, frame_end);
    found = true;

    if (frame_end > p + 1)
      p = frame_end - 1;
  }

  if (!found)
    return;

  hp_publish_discovered_device_ids();
}
