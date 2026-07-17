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
static const char *const HUAWEI_PROP_FC41_FRAME_TAG = "huawei_prop_fc41_frame";
static const char *const HUAWEI_MODBUS_TAG = "huawei_modbus";

static constexpr uint16_t HP_TAG_LINE_VOLTAGE = 0x774D;       // A-B, B-C, C-A line voltages (×0.1 V)
static constexpr uint16_t HP_TAG_METER_PHASE_VOLTAGE_AUX = 0x9C52;  // meter-only candidate phase voltages
static constexpr uint16_t HP_TAG_GRID_FREQ_PHASE_V = 0x7733;  // count=4: freq(/100 Hz), A/B/C V (×0.1)
static constexpr uint16_t HP_TAG_METER_PHASE_VOLTAGE = 0x7734;  // meter-only phase voltages A/B/C (×0.1 V)
static constexpr uint16_t HP_TAG_PHASE_VOLTAGE_TLV = 0x7EC5;
static constexpr uint16_t HP_TAG_PHASE_VOLTAGE_U32 = 0x90ED;
static constexpr uint16_t HP_TAG_PHASE_CURRENT = 0x7D48;    // 0x35 register window, A/B/C amps (×0.001)
static constexpr uint16_t HP_TAG_METER_PHASE_VOLTAGE_ALT = 0x7729;  // meter-only candidate; count varies
static constexpr uint16_t HP_TAG_LINE_VOLTAGE_U32 = 0x9106;    // u32 line-voltage payload (×0.1 V)
static constexpr uint16_t HP_TAG_ACTIVE_POWER = 0x7D5F;     // 0x35 words=2 i32, active power W
static constexpr uint16_t HP_TAG_REACTIVE_POWER = 0x7D66;   // 0x35 words=2, var (signed)
static constexpr uint16_t HP_TAG_INVERTER_CANDIDATE_93C5 = 0x93C5;  // observed i32; semantics unknown
static constexpr uint16_t HP_TAG_TOTAL_YIELD = 0x7D6A;      // 0x35 words=2 u32, lifetime kWh (×0.01)
static constexpr uint16_t HP_TAG_DAILY_ENERGY = 0x7D72;     // u32 inside a bounded 0x35 register window (×0.01)
static constexpr uint16_t HP_TAG_DAILY_DC_ENERGY_YIELD = 0x7D80;  // u32, current-day DC yield (×0.01 kWh)
static constexpr uint16_t HP_TAG_MPPT1_TOTAL_DC_ENERGY_YIELD = 0x7DD4;  // u32, MPPT1 lifetime DC yield (×0.01 kWh)
static constexpr uint16_t HP_REG_PV1_VOLTAGE = 0x7D10;
static constexpr uint16_t HP_REG_PV1_CURRENT = 0x7D11;
static constexpr uint16_t HP_REG_INPUT_POWER = 0x7D40;
static constexpr uint16_t HP_REG_LINE_AB_VOLTAGE = 0x7D42;
static constexpr uint16_t HP_REG_LINE_BC_VOLTAGE = 0x7D43;
static constexpr uint16_t HP_REG_LINE_CA_VOLTAGE = 0x7D44;
static constexpr uint16_t HP_REG_PHASE_A_VOLTAGE = 0x7D45;
static constexpr uint16_t HP_REG_PHASE_B_VOLTAGE = 0x7D46;
static constexpr uint16_t HP_REG_PHASE_C_VOLTAGE = 0x7D47;
static constexpr uint16_t HP_REG_PHASE_A_CURRENT = 0x7D48;
static constexpr uint16_t HP_REG_PHASE_B_CURRENT = 0x7D4A;
static constexpr uint16_t HP_REG_PHASE_C_CURRENT = 0x7D4C;
static constexpr uint16_t HP_REG_ACTIVE_POWER_STANDARD = 0x7D50;
static constexpr uint16_t HP_REG_REACTIVE_POWER_STANDARD = 0x7D52;
static constexpr uint16_t HP_REG_POWER_FACTOR = 0x7D54;
static constexpr uint16_t HP_REG_GRID_FREQUENCY = 0x7D55;
static constexpr uint16_t HP_REG_INTERNAL_TEMPERATURE = 0x7D57;
static constexpr uint16_t HP_REG_INSULATION_RESISTANCE = 0x7D58;
static constexpr uint16_t HP_REG_INVERTER_STATUS = 0x7D59;
static constexpr uint16_t HP_REG_INVERTER_FAULT_CODE = 0x7D5A;
static constexpr uint16_t HP_REG_STARTUP_TIME = 0x7D5B;
static constexpr uint16_t HP_REG_SHUTDOWN_TIME = 0x7D5D;
static constexpr uint16_t HP_REG_INVERTER_ALARM_1 = 0x7D08;
static constexpr uint16_t HP_REG_INVERTER_ALARM_2 = 0x7D09;
static constexpr uint16_t HP_REG_INVERTER_ALARM_3 = 0x7D0A;
static constexpr uint16_t HP_REG_METER_PHASE_A_CURRENT = 0x90F3;
static constexpr uint16_t HP_REG_METER_PHASE_B_CURRENT = 0x90F5;
static constexpr uint16_t HP_REG_METER_PHASE_C_CURRENT = 0x90F7;
static constexpr uint16_t HP_REG_METER_ACTIVE_POWER = 0x90F9;
static constexpr uint16_t HP_REG_METER_REACTIVE_POWER = 0x90FB;
static constexpr uint16_t HP_REG_METER_POWER_FACTOR = 0x90FD;
static constexpr uint16_t HP_REG_METER_PHASE_A_ACTIVE_POWER = 0x910C;
static constexpr uint16_t HP_REG_METER_PHASE_B_ACTIVE_POWER = 0x910E;
static constexpr uint16_t HP_REG_METER_PHASE_C_ACTIVE_POWER = 0x9110;
static constexpr uint16_t HP_TAG_INVERTER_CAPABILITIES = 0x7574;
static constexpr uint16_t HP_REG_RATED_POWER = 0x7579;
static constexpr uint16_t HP_TAG_INVERTER_MODEL_FAMILY = 0x7589;  // duplicate identity field; log only
static constexpr uint16_t HP_TAG_MONITORING_BOARD_SERIAL = 0x7927;  // duplicate identity metadata; log only
static constexpr uint16_t HP_TAG_DC_MBUS_VERSION = 0x798B;  // duplicate software metadata; log only
static constexpr uint16_t HP_TAG_BATTERY_CHARGED_TODAY = 0x9097;   // u32, kWh (x0.01)
static constexpr uint16_t HP_TAG_BATTERY_DISCHARGED_TODAY = 0x9099;  // u32, kWh (x0.01)
static constexpr uint16_t HP_TAG_BATTERY_CHARGED_TODAY_LEGACY = 0x9398;
static constexpr uint16_t HP_TAG_BATTERY_DISCHARGED_TODAY_LEGACY = 0x939A;
static constexpr uint16_t HP_TAG_BATTERY_TOTAL_CHARGE = 0x9394;  // legacy/direct u32, kWh (x0.01)
static constexpr uint16_t HP_TAG_BATTERY_TOTAL_BLOCK = 0x90CA;
static constexpr uint16_t HP_TAG_BATTERY_TOTAL_DISCHARGE = 0x90CC;
static constexpr uint16_t HP_TAG_BATTERY_RATED_CAPACITY = 0x90CE;
static constexpr uint16_t HP_TAG_BATTERY_POWER = 0x9089;  // i32, charge/discharge W
static constexpr uint16_t HP_TAG_BATTERY_BUS_VOLTAGE = 0x908B;
static constexpr uint16_t HP_TAG_BATTERY_SOC = 0x908C;
static constexpr uint16_t HP_REG_BATTERY_RUNNING_STATUS = 0x9088;
static constexpr uint16_t HP_REG_BATTERY_WORKING_MODE = 0x908E;
static constexpr uint16_t HP_TAG_BATTERY_BUS_CURRENT = 0x909D;
static constexpr uint16_t HP_TAG_BATTERY_INTERNAL_TEMPERATURE = 0x909E;
static constexpr uint16_t HP_REG_BATTERY_REMAINING_CHARGE_DISCHARGE_TIME = 0x90A1;  // minutes; log only
static constexpr uint16_t HP_REG_BATTERY_MAXIMUM_CHARGE_POWER = 0xB7E3;
static constexpr uint16_t HP_REG_BATTERY_MAXIMUM_DISCHARGE_POWER = 0xB7E5;
static constexpr uint16_t HP_REG_BATTERY_END_OF_CHARGE_SOC = 0xB7E9;
static constexpr uint16_t HP_REG_BATTERY_END_OF_DISCHARGE_SOC = 0xB7EA;
static constexpr uint16_t HP_REG_BATTERY_FORCED_PERIOD = 0xB7EB;
static constexpr uint16_t HP_REG_BATTERY_FORCED_POWER = 0xB7EC;
static constexpr uint16_t HP_REG_BATTERY_WORKING_MODE_SETTING = 0xB7EE;
static constexpr uint16_t HP_REG_BATTERY_CHARGE_FROM_GRID = 0xB7EF;
static constexpr uint16_t HP_REG_BATTERY_GRID_CHARGE_CUTOFF_SOC = 0xB7F0;
static constexpr uint16_t HP_REG_BATTERY_TOU_PERIODS = 0xB897;
static constexpr size_t HP_BATTERY_TOU_PERIOD_WORDS = 43;
static constexpr size_t HP_BATTERY_TOU_MAX_PERIODS = 14;
static constexpr uint16_t HP_TAG_INVERTER_SOLAR_PV = 0x7E44;         // 0x35 words=4 i32[2], PV1/PV2 watts
static constexpr uint16_t HP_TAG_SECONDARY_ACTIVE_POWER = 0x9385;  // inverter secondary active-power candidate
static constexpr uint16_t HP_TAG_BUS_CLOCK = 0x7D6E;             // variable words: u32 unix sec, optional u32 seq
static constexpr uint16_t HP_TAG_SERIAL_NUMBER = 0x9187;         // word payload containing ASCII serial number
static constexpr uint16_t HP_TAG_INVERTER_IDENTIFICATION = 0x7530;  // model, serial number, software version
static constexpr uint16_t HP_TAG_BATTERY_PACK_1_SN_FW = 0x9538;  // ASCII SN plus firmware version
static constexpr uint16_t HP_TAG_BATTERY_PACK_2_SN_FW = 0x9559;  // ASCII SN plus firmware version
static constexpr uint16_t HP_TAG_BATTERY_PACK_3_SN_FW = 0x9583;  // ASCII SN plus optional firmware version
static constexpr uint16_t HP_TAG_BATTERY_PACK_MODEL = 0x9640;    // model field followed by crypto metadata
static constexpr uint16_t HP_TAG_BATTERY_PACK_MODELS = 0x985B;  // three fixed-width battery-pack model fields
static constexpr uint16_t HP_TAG_BATTERY_PACK_1_HIGHEST_TEMP = 0x9634;  // ×0.1 °C
static constexpr uint16_t HP_TAG_BATTERY_PACK_1_LOWEST_TEMP = 0x9635;   // ×0.1 °C
static constexpr uint16_t HP_TAG_BATTERY_PACK_2_HIGHEST_TEMP = 0x9636;  // ×0.1 °C
static constexpr uint16_t HP_TAG_BATTERY_PACK_2_LOWEST_TEMP = 0x9637;   // ×0.1 °C
static constexpr uint16_t HP_TAG_BATTERY_PACK_3_HIGHEST_TEMP = 0x9638;  // ×0.1 °C
static constexpr uint16_t HP_TAG_BATTERY_PACK_3_LOWEST_TEMP = 0x9639;   // ×0.1 °C
static constexpr uint16_t HP_TAG_BATTERY_PACK_1_SOC = 0x9555;
static constexpr uint16_t HP_TAG_BATTERY_PACK_1_POWER = 0x9559;
static constexpr uint16_t HP_TAG_BATTERY_PACK_1_VOLTAGE = 0x955B;
static constexpr uint16_t HP_TAG_BATTERY_PACK_1_TOTAL_ENERGY = 0x9560;
static constexpr uint16_t HP_TAG_BATTERY_PACK_2_SOC = 0x957F;
static constexpr uint16_t HP_TAG_BATTERY_PACK_2_POWER = 0x9583;
static constexpr uint16_t HP_TAG_BATTERY_PACK_2_VOLTAGE = 0x9585;
static constexpr uint16_t HP_TAG_BATTERY_PACK_2_TOTAL_ENERGY = 0x958A;
static constexpr uint16_t HP_TAG_BATTERY_PACK_3_SOC = 0x95A9;
static constexpr uint16_t HP_TAG_BATTERY_PACK_3_POWER = 0x95AD;
static constexpr uint16_t HP_TAG_BATTERY_PACK_3_VOLTAGE = 0x95AF;
static constexpr uint16_t HP_TAG_BATTERY_PACK_3_TOTAL_ENERGY = 0x95B4;
static constexpr size_t HP_BATTERY_MODULE_MODEL_FIELD_BYTES = 30;
static constexpr size_t HP_BATTERY_PACK_COUNT = 3;
static constexpr float HP_ENERGY_YIELD_SCALE = 0.01f;
static constexpr float HP_LINE_VOLTAGE_SCALE = 0.1f;
static constexpr float HP_PHASE_VOLTAGE_SCALE = 0.1f;
static constexpr float HP_PHASE_CURRENT_SCALE = 0.001f;
static constexpr float HP_METER_PHASE_CURRENT_SCALE = 0.01f;
static constexpr float HP_GRID_FREQUENCY_SCALE = 0.01f;
static constexpr float HP_TEMPERATURE_SCALE = 0.1f;
static constexpr float HP_BATTERY_VOLTAGE_SCALE = 0.1f;
static constexpr float HP_BATTERY_CURRENT_SCALE = 0.1f;
static constexpr float HP_BATTERY_SOC_SCALE = 0.1f;
static constexpr float HP_BATTERY_CAPACITY_SCALE = 0.001f;
static constexpr size_t HP_DISCOVERED_DEVICE_SLOTS = 16;
static constexpr size_t HP_DISCOVERED_TAG_SLOTS = 128;
static constexpr size_t HP_TAG_DEVICE_SLOTS = 8;
static constexpr size_t HP_KNOWN_TAG_PAGES = 5;
static constexpr size_t HP_INVERTER_TAG_PAGES = 10;
static constexpr size_t HP_UNKNOWN_TAG_PAGES = 6;
static constexpr size_t HP_TAG_PAGE_MAX_CHARS = 240;
static constexpr size_t HP_LATEST_VALUE_MAX_CHARS = 12;
static constexpr size_t HP_UART_PENDING_MAX = 2048;
static constexpr size_t HP_MODBUS_UART_PENDING_MAX = 2048;

static constexpr uint8_t HP_FC41_SUBFN_UPLOAD_START = 0x05;
static constexpr uint8_t HP_FC41_SUBFN_UPLOAD_DATA = 0x06;
static constexpr uint8_t HP_FC41_SUBFN_UPLOAD_COMPLETE = 0x0C;
static constexpr uint8_t HP_FC41_SUBFN_CURRENT_DATA = 0x35;
static constexpr uint8_t HP_FC41_SUBFN_REPORT = 0x89;
static constexpr uint8_t HP_FC41_EXCEPTION_FUNCTION = 0xC1;

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

struct HpDiscoveredTag {
  uint16_t tag{0};
  uint8_t devices[HP_TAG_DEVICE_SLOTS]{};
  bool shared_value_logged[HP_TAG_DEVICE_SLOTS]{};
  size_t device_count{0};
};

struct HpLatestTagValue {
  uint8_t device_id{0};
  uint16_t tag{0};
  std::string value;
};

static HpDiscoveredTag hp_discovered_tags[HP_DISCOVERED_TAG_SLOTS];
static size_t hp_discovered_tag_count = 0;
static std::vector<HpLatestTagValue> hp_latest_tag_values;
static uint8_t hp_known_tag_values_dirty = 0;
static bool hp_unknown_tag_values_dirty = false;
static std::vector<uint8_t> hp_uart_pending;
static std::vector<uint8_t> hp_modbus_uart_pending;
static std::string hp_last_device_ids;

struct HpModbusReadRequest {
  bool valid{false};
  uint8_t device_id{0};
  uint8_t function{0};
  uint16_t start_register{0};
  uint16_t register_count{0};
};

static HpModbusReadRequest hp_modbus_last_read_request;

static inline bool hp_tlv_type_size(uint8_t type, size_t *out_size);
static inline uint32_t hp_u32be(const std::vector<uint8_t> &bytes, size_t pos);
static inline void hp_publish_optional_text_sensor(const char *entity_name, const std::string &value);
static inline bool hp_tag_has_established_semantics(uint8_t device_id, uint16_t tag);

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

static inline void hp_format_inverter_event_time(uint32_t unix_sec, char *out, size_t out_size) {
  if (unix_sec == 0 || unix_sec == UINT32_MAX) {
    snprintf(out, out_size, "N/A");
    return;
  }
  if (!hp_unix_time_looks_valid(unix_sec)) {
    snprintf(out, out_size, "raw=%lu (0x%08lX)", static_cast<unsigned long>(unix_sec),
             static_cast<unsigned long>(unix_sec));
    return;
  }
  const time_t t = static_cast<time_t>(unix_sec);
  struct tm tm_buf;
  if (gmtime_r(&t, &tm_buf) == nullptr) {
    snprintf(out, out_size, "raw=%lu", static_cast<unsigned long>(unix_sec));
    return;
  }
  char time_buf[32];
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
  snprintf(out, out_size, "%s UTC", time_buf);
}

static inline void hp_publish_total_yield(uint8_t device_id, uint32_t raw_wh) {
  if (device_id != HP_DEV_INVERTER)
    return;
  const float kwh = static_cast<float>(raw_wh) * HP_ENERGY_YIELD_SCALE;
  id(inverter_total_yield).publish_state(kwh);
}

static inline void hp_publish_daily_energy(uint8_t device_id, uint32_t raw_wh) {
  if (device_id != HP_DEV_INVERTER)
    return;
  const float kwh = static_cast<float>(raw_wh) * HP_ENERGY_YIELD_SCALE;
  id(inverter_daily_energy).publish_state(kwh);
}

static inline void hp_publish_daily_dc_energy_yield(uint8_t device_id, uint32_t raw_value) {
  if (device_id != HP_DEV_INVERTER)
    return;
  id(inverter_daily_dc_energy_yield).publish_state(
      static_cast<float>(raw_value) * HP_ENERGY_YIELD_SCALE);
}

static inline void hp_publish_mppt1_total_dc_energy_yield(uint8_t device_id, uint32_t raw_value) {
  if (device_id != HP_DEV_INVERTER)
    return;
  id(inverter_mppt1_total_dc_energy_yield).publish_state(
      static_cast<float>(raw_value) * HP_ENERGY_YIELD_SCALE);
}

static inline void hp_try_publish_bus_clock_value(uint8_t device_id, uint16_t tag,
                                                  const std::vector<uint8_t> &bytes, size_t pos,
                                                  size_t val_size) {
  if (tag != HP_TAG_BUS_CLOCK || val_size < 4 || pos + 3 + val_size > bytes.size())
    return;

  const uint32_t unix_sec = hp_u32be(bytes, pos + 3);

  char buf[48];
  hp_format_unix_local_time(unix_sec, buf, sizeof(buf));

  switch (device_id) {
    case HP_DEV_INVERTER:
      id(inverter_bus_clock).publish_state(buf);
      break;
    default:
      break;
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

  if (out != hp_last_device_ids) {
    id(huawei41_device_ids).publish_state(out);
    hp_last_device_ids = out;
  }
}

static inline void hp_publish_tag_page(uint8_t device_id, size_t page, const std::string &value) {
  const char *role = hp_device_role_name(device_id);
  if (role == nullptr)
    return;
  char entity_name[72];
  snprintf(entity_name, sizeof(entity_name), "ZZ Reverse Engineering - %s FC41 Tags %u", role,
           static_cast<unsigned>(page + 1));
  hp_publish_optional_text_sensor(entity_name, value.empty() ? "(none yet)" : value);
}

static inline std::string hp_compact_latest_value(const std::string &raw_hex) {
  std::string value;
  value.reserve(std::min(raw_hex.size(), HP_LATEST_VALUE_MAX_CHARS));
  for (char ch : raw_hex) {
    if (ch == ' ')
      continue;
    value += ch;
    if (value.size() >= HP_LATEST_VALUE_MAX_CHARS)
      break;
  }
  return value.empty() ? "?" : value;
}

static inline bool hp_update_latest_tag_value(uint8_t device_id, uint16_t tag, const std::string &raw_hex) {
  const std::string value = hp_compact_latest_value(raw_hex);
  for (HpLatestTagValue &record : hp_latest_tag_values) {
    if (record.device_id == device_id && record.tag == tag) {
      if (record.value == value)
        return false;
      record.value = value;
      return true;
    }
  }
  if (hp_latest_tag_values.capacity() == 0)
    hp_latest_tag_values.reserve(HP_DISCOVERED_TAG_SLOTS);
  if (hp_latest_tag_values.size() >= HP_DISCOVERED_TAG_SLOTS * HP_TAG_DEVICE_SLOTS)
    return false;
  hp_latest_tag_values.push_back({device_id, tag, value});
  return true;
}

static inline const std::string &hp_latest_tag_value(uint8_t device_id, uint16_t tag) {
  static const std::string unknown{"?"};
  for (const HpLatestTagValue &record : hp_latest_tag_values) {
    if (record.device_id == device_id && record.tag == tag)
      return record.value;
  }
  return unknown;
}

static inline bool hp_try_append_page(std::string &page, const std::string &entry, const char *separator) {
  const size_t separator_size = page.empty() ? 0 : strlen(separator);
  if (page.size() + separator_size + entry.size() > HP_TAG_PAGE_MAX_CHARS)
    return false;
  if (!page.empty())
    page += separator;
  page += entry;
  return true;
}

static inline void hp_publish_discovered_tags_for_device(uint8_t device_id) {
  uint16_t sorted[HP_DISCOVERED_TAG_SLOTS];
  size_t count = 0;
  for (size_t i = 0; i < hp_discovered_tag_count; i++) {
    for (size_t j = 0; j < hp_discovered_tags[i].device_count; j++) {
      if (hp_discovered_tags[i].devices[j] == device_id) {
        const uint16_t tag = hp_discovered_tags[i].tag;
        if (!hp_tag_has_established_semantics(device_id, tag))
          sorted[count++] = tag;
        break;
      }
    }
  }
  std::sort(sorted, sorted + count);

  const size_t page_count = device_id == HP_DEV_INVERTER ? HP_INVERTER_TAG_PAGES : HP_KNOWN_TAG_PAGES;
  std::vector<std::string> pages(page_count);
  size_t page = 0;
  char entry_buf[32];
  for (size_t i = 0; i < count; i++) {
    snprintf(entry_buf, sizeof(entry_buf), "%04X=%s", sorted[i], hp_latest_tag_value(device_id, sorted[i]).c_str());
    if (!hp_try_append_page(pages[page], entry_buf, ", ")) {
      if (++page >= pages.size()) {
        if (pages.back().size() + 4 <= HP_TAG_PAGE_MAX_CHARS)
          pages.back() += " ...";
        break;
      }
      hp_try_append_page(pages[page], entry_buf, ", ");
    }
  }
  for (size_t i = 0; i < pages.size(); i++)
    hp_publish_tag_page(device_id, i, pages[i]);
}

static inline void hp_publish_shared_tags() {
  std::string out;
  char item[64];
  for (size_t i = 0; i < hp_discovered_tag_count; i++) {
    const HpDiscoveredTag &record = hp_discovered_tags[i];
    if (record.device_count < 2)
      continue;
    snprintf(item, sizeof(item), "%04X[", record.tag);
    std::string entry = item;
    for (size_t j = 0; j < record.device_count; j++) {
      snprintf(item, sizeof(item), "%s%u", j ? "," : "", record.devices[j]);
      entry += item;
    }
    entry += "]";
    if (!out.empty() && out.size() + 1 + entry.size() > 240) {
      out += " ...";
      break;
    }
    if (!out.empty())
      out += " ";
    out += entry;
  }
  id(huawei41_shared_tags).publish_state(out.empty() ? "(none observed)" : out.c_str());
}

// Optional/newer entities are resolved at runtime instead of referenced with
// id(...). This keeps cached older YAML compatible with newer decoder source.
static inline void hp_publish_optional_text_sensor(const char *entity_name, const std::string &value) {
  const uint32_t key = esphome::fnv1_hash_object_id(entity_name, strlen(entity_name));
  for (auto *sensor : App.get_text_sensors()) {
    if (sensor->get_object_id_hash() == key) {
      sensor->publish_state(value);
      return;
    }
  }
}

static inline void hp_publish_unknown_tag_page(size_t page, const std::string &value) {
  char entity_name[72];
  snprintf(entity_name, sizeof(entity_name), "ZZ Reverse Engineering - Unknown FC41 Device Tags %u",
           static_cast<unsigned>(page + 1));
  hp_publish_optional_text_sensor(entity_name, value.empty() ? "(none yet)" : value);
}

static inline void hp_publish_unknown_tag_pages() {
  std::vector<uint32_t> pairs;
  pairs.reserve(hp_discovered_tag_count);
  for (size_t i = 0; i < hp_discovered_tag_count; i++) {
    const HpDiscoveredTag &record = hp_discovered_tags[i];
    for (size_t j = 0; j < record.device_count; j++) {
      const uint8_t device_id = record.devices[j];
      if (hp_device_role_name(device_id) == nullptr)
        pairs.push_back((static_cast<uint32_t>(device_id) << 16) | record.tag);
    }
  }
  std::sort(pairs.begin(), pairs.end());

  std::vector<std::string> pages(HP_UNKNOWN_TAG_PAGES);
  size_t page = 0;
  int current_device = -1;
  char tag_buf[32];
  for (uint32_t pair : pairs) {
    const uint8_t device_id = static_cast<uint8_t>(pair >> 16);
    const uint16_t tag = static_cast<uint16_t>(pair);
    snprintf(tag_buf, sizeof(tag_buf), "%04X=%s", tag, hp_latest_tag_value(device_id, tag).c_str());
    const bool new_device = current_device != device_id;
    std::string entry;
    if (new_device) {
      char prefix[12];
      snprintf(prefix, sizeof(prefix), "0x%02X: ", device_id);
      entry = prefix;
    }
    entry += tag_buf;
    const char *separator = new_device ? " | " : ", ";
    if (!hp_try_append_page(pages[page], entry, separator)) {
      if (++page >= pages.size()) {
        if (pages.back().size() + 4 <= HP_TAG_PAGE_MAX_CHARS)
          pages.back() += " ...";
        break;
      }
      char prefix[12];
      snprintf(prefix, sizeof(prefix), "0x%02X: ", device_id);
      entry = prefix;
      entry += tag_buf;
      hp_try_append_page(pages[page], entry, " | ");
    }
    current_device = device_id;
  }
  for (size_t i = 0; i < pages.size(); i++)
    hp_publish_unknown_tag_page(i, pages[i]);
}

static inline void hp_mark_tag_values_dirty(uint8_t device_id) {
  if (device_id == HP_DEV_EMMA)
    hp_known_tag_values_dirty |= 0x01;
  else if (device_id == HP_DEV_METER)
    hp_known_tag_values_dirty |= 0x02;
  else if (device_id == HP_DEV_INVERTER)
    hp_known_tag_values_dirty |= 0x04;
  else
    hp_unknown_tag_values_dirty = true;
}

static inline void hp_publish_dirty_tag_values() {
  if (hp_known_tag_values_dirty & 0x01)
    hp_publish_discovered_tags_for_device(HP_DEV_EMMA);
  if (hp_known_tag_values_dirty & 0x02)
    hp_publish_discovered_tags_for_device(HP_DEV_METER);
  if (hp_known_tag_values_dirty & 0x04)
    hp_publish_discovered_tags_for_device(HP_DEV_INVERTER);
  if (hp_unknown_tag_values_dirty)
    hp_publish_unknown_tag_pages();
  hp_known_tag_values_dirty = 0;
  hp_unknown_tag_values_dirty = false;
}

static inline void hp_note_tag_observation(uint8_t device_id, uint16_t tag, const std::string &decoded,
                                           const std::string &raw_value) {
  HpDiscoveredTag *record = nullptr;
  for (size_t i = 0; i < hp_discovered_tag_count; i++) {
    if (hp_discovered_tags[i].tag == tag) {
      record = &hp_discovered_tags[i];
      break;
    }
  }
  if (record == nullptr) {
    if (hp_discovered_tag_count >= HP_DISCOVERED_TAG_SLOTS)
      return;
    record = &hp_discovered_tags[hp_discovered_tag_count++];
    record->tag = tag;
  }

  size_t device_slot = record->device_count;
  for (size_t i = 0; i < record->device_count; i++) {
    if (record->devices[i] == device_id) {
      device_slot = i;
      break;
    }
  }

  if (device_slot == record->device_count) {
    if (record->device_count >= HP_TAG_DEVICE_SLOTS)
      return;
    record->devices[device_slot] = device_id;
    record->shared_value_logged[device_slot] = false;
    record->device_count++;
    if (record->device_count == 2) {
      for (size_t i = 0; i < record->device_count; i++)
        record->shared_value_logged[i] = false;
    }
    hp_publish_shared_tags();
  }

  if (hp_update_latest_tag_value(device_id, tag, raw_value))
    hp_mark_tag_values_dirty(device_id);

  if (record->device_count > 1 && !record->shared_value_logged[device_slot]) {
    ESP_LOGI(HUAWEI_PROP_TAG, "Shared FC41 tag sample dev=%u (0x%02X) tag=0x%04X :: %s", device_id, device_id, tag,
             decoded.c_str());
    record->shared_value_logged[device_slot] = true;
  }
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
  return (tag >= 0x7700 && tag <= 0x77FF) || tag == HP_TAG_METER_PHASE_VOLTAGE_AUX;
}

static inline bool hp_tlv_tag_looks_valid(uint16_t tag) {
  if (tag < 0x0100)
    return false;
  if (hp_is_count_u16_tag(tag))
    return true;
  if (tag >= 0x7500 && tag <= 0x7EFF)
    return true;
  if (tag >= 0x9000 && tag <= 0x93FF)
    return true;
  if (tag >= 0x9100 && tag <= 0x91FF)
    return true;
  if (tag >= 0x9400 && tag <= 0xBBFF)
    return true;
  return false;
}

static inline int16_t hp_i16be(uint8_t hi, uint8_t lo) {
  return static_cast<int16_t>(hp_u16be(hi, lo));
}

// A current-data item is a contiguous register window. A known field is safe
// to read only when every word of that field is inside the advertised window.
static inline bool hp_current_block_field_offset(uint16_t block_start, uint8_t word_count, uint16_t field_register,
                                                 size_t field_words, size_t *byte_offset) {
  if (field_register < block_start || field_words == 0)
    return false;
  const uint32_t offset_words = static_cast<uint32_t>(field_register) - block_start;
  if (offset_words + field_words > word_count)
    return false;
  *byte_offset = static_cast<size_t>(offset_words) * 2;
  return true;
}

static inline bool hp_current_block_u16(const std::vector<uint8_t> &bytes, size_t value_start, size_t val_size,
                                        uint16_t block_start, uint8_t word_count, uint16_t field_register,
                                        uint16_t *value) {
  size_t offset = 0;
  if (!hp_current_block_field_offset(block_start, word_count, field_register, 1, &offset) ||
      offset + 2 > val_size || value_start + offset + 2 > bytes.size())
    return false;
  *value = hp_u16be(bytes[value_start + offset], bytes[value_start + offset + 1]);
  return true;
}

static inline bool hp_current_block_i16(const std::vector<uint8_t> &bytes, size_t value_start, size_t val_size,
                                        uint16_t block_start, uint8_t word_count, uint16_t field_register,
                                        int16_t *value) {
  uint16_t raw = 0;
  if (!hp_current_block_u16(bytes, value_start, val_size, block_start, word_count, field_register, &raw))
    return false;
  *value = static_cast<int16_t>(raw);
  return true;
}

static inline bool hp_current_block_u32(const std::vector<uint8_t> &bytes, size_t value_start, size_t val_size,
                                        uint16_t block_start, uint8_t word_count, uint16_t field_register,
                                        uint32_t *value) {
  size_t offset = 0;
  if (!hp_current_block_field_offset(block_start, word_count, field_register, 2, &offset) ||
      offset + 4 > val_size || value_start + offset + 4 > bytes.size())
    return false;
  *value = hp_u32be(bytes, value_start + offset);
  return true;
}

static inline bool hp_current_block_i32(const std::vector<uint8_t> &bytes, size_t value_start, size_t val_size,
                                        uint16_t block_start, uint8_t word_count, uint16_t field_register,
                                        int32_t *value) {
  uint32_t raw = 0;
  if (!hp_current_block_u32(bytes, value_start, val_size, block_start, word_count, field_register, &raw))
    return false;
  *value = static_cast<int32_t>(raw);
  return true;
}

static inline bool hp_tlv_inline_length_tag(uint16_t tag) {
  switch (tag) {
    case HP_TAG_SERIAL_NUMBER:
    case HP_TAG_INVERTER_IDENTIFICATION:
    case HP_TAG_BATTERY_PACK_1_SN_FW:
    case HP_TAG_BATTERY_PACK_2_SN_FW:
    case HP_TAG_BATTERY_PACK_3_SN_FW:
    case HP_TAG_BATTERY_PACK_MODEL:
    case HP_TAG_BATTERY_PACK_MODELS:
      return true;
    default:
      return false;
  }
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
      // Values >= 0x20 may be inline byte lengths, but callers must also
      // validate that the tag is a known variable-length field.
      if (type >= 0x20) {
        *out_size = type;
        return true;
      }
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

template<typename T> static inline void hp_publish_line_voltage_sensors(uint8_t device_id, const T *values) {
  if (device_id == HP_DEV_METER) {
    id(meter_line_ab_voltage).publish_state(static_cast<float>(values[0]) * HP_LINE_VOLTAGE_SCALE);
    id(meter_line_bc_voltage).publish_state(static_cast<float>(values[1]) * HP_LINE_VOLTAGE_SCALE);
    id(meter_line_ca_voltage).publish_state(static_cast<float>(values[2]) * HP_LINE_VOLTAGE_SCALE);
  } else if (device_id == HP_DEV_INVERTER) {
    id(inverter_line_ab_voltage).publish_state(static_cast<float>(values[0]) * HP_LINE_VOLTAGE_SCALE);
    id(inverter_line_bc_voltage).publish_state(static_cast<float>(values[1]) * HP_LINE_VOLTAGE_SCALE);
    id(inverter_line_ca_voltage).publish_state(static_cast<float>(values[2]) * HP_LINE_VOLTAGE_SCALE);
  }
}

template<typename T> static inline void hp_publish_phase_voltage_sensors(uint8_t device_id, const T *values) {
  if (device_id != HP_DEV_INVERTER)
    return;
  id(inverter_phase_a_voltage).publish_state(static_cast<float>(values[0]) * HP_PHASE_VOLTAGE_SCALE);
  id(inverter_phase_b_voltage).publish_state(static_cast<float>(values[1]) * HP_PHASE_VOLTAGE_SCALE);
  id(inverter_phase_c_voltage).publish_state(static_cast<float>(values[2]) * HP_PHASE_VOLTAGE_SCALE);
}

static inline void hp_publish_phase_current_sensors(uint8_t device_id, const int32_t *amps_raw) {
  if (device_id != HP_DEV_INVERTER)
    return;
  id(inverter_phase_a_current).publish_state(static_cast<float>(amps_raw[0]) * HP_PHASE_CURRENT_SCALE);
  id(inverter_phase_b_current).publish_state(static_cast<float>(amps_raw[1]) * HP_PHASE_CURRENT_SCALE);
  id(inverter_phase_c_current).publish_state(static_cast<float>(amps_raw[2]) * HP_PHASE_CURRENT_SCALE);
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

  if (device_id == HP_DEV_METER && tag == HP_TAG_LINE_VOLTAGE) {
    hp_publish_line_voltage_sensors(device_id, values);
  }
}

static inline void hp_try_publish_phase_sensors(uint8_t device_id, const std::vector<uint8_t> &bytes,
                                                size_t payload_start, size_t payload_end) {
  uint16_t line_v[3]{};
  uint16_t grid_phase_v[4]{};
  const bool has_grid_phase =
      hp_find_count_u16_tag(bytes, payload_start, payload_end, HP_TAG_GRID_FREQ_PHASE_V, 4, grid_phase_v);
  const bool has_line =
      hp_find_count_u16_tag(bytes, payload_start, payload_end, HP_TAG_LINE_VOLTAGE, 3, line_v);
  if (has_grid_phase)
    hp_publish_grid_freq_and_phase_v(device_id, grid_phase_v);
  if (has_line)
    hp_publish_line_voltage_sensors(device_id, line_v);
}

static inline std::string hp_format_count_tag(uint8_t device_id, uint16_t tag, const uint16_t *values, uint16_t count) {
  std::string out;
  char buf[48];

  if (device_id == HP_DEV_METER && tag == HP_TAG_LINE_VOLTAGE && count == 3) {
    snprintf(buf, sizeof(buf), "tag=0x%04X line_V AB=%.1f BC=%.1f CA=%.1f", tag,
             values[0] * HP_LINE_VOLTAGE_SCALE, values[1] * HP_LINE_VOLTAGE_SCALE,
             values[2] * HP_LINE_VOLTAGE_SCALE);
    return buf;
  }
  if (device_id == HP_DEV_METER && tag == HP_TAG_GRID_FREQ_PHASE_V && count == 4) {
    snprintf(buf, sizeof(buf), "tag=0x%04X freq=%.2fHz A=%.1fV B=%.1fV C=%.1fV", tag,
             values[0] * HP_GRID_FREQUENCY_SCALE, values[1] * HP_PHASE_VOLTAGE_SCALE,
             values[2] * HP_PHASE_VOLTAGE_SCALE, values[3] * HP_PHASE_VOLTAGE_SCALE);
    return buf;
  }
  if (device_id == HP_DEV_METER &&
      (tag == HP_TAG_METER_PHASE_VOLTAGE_AUX || tag == HP_TAG_METER_PHASE_VOLTAGE ||
       tag == HP_TAG_METER_PHASE_VOLTAGE_ALT) && count == 3) {
    snprintf(buf, sizeof(buf), "tag=0x%04X candidate_phase_V A=%.1f B=%.1f C=%.1f", tag,
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

static inline void hp_tlv_ascii_runs(const std::vector<uint8_t> &bytes, size_t start, size_t len,
                                     std::vector<std::string> &runs) {
  std::string run;
  const size_t end = std::min(bytes.size(), start + len);
  for (size_t i = start; i < end; i++) {
    const uint8_t b = bytes[i];
    if (b >= 0x20 && b <= 0x7E) {
      run += static_cast<char>(b);
      continue;
    }
    if (!run.empty()) {
      runs.push_back(run);
      run.clear();
    }
  }
  if (!run.empty())
    runs.push_back(run);
}

static inline std::string hp_tlv_value_to_ascii_runs(const std::vector<uint8_t> &bytes, size_t start, size_t len) {
  std::vector<std::string> runs;
  hp_tlv_ascii_runs(bytes, start, len, runs);
  std::string out;
  for (const std::string &run : runs) {
    if (!out.empty())
      out += ";";
    out += run;
  }
  return out;
}

static inline bool hp_string_starts_with(const std::string &value, const char *prefix) {
  const size_t prefix_len = strlen(prefix);
  return value.size() >= prefix_len && value.compare(0, prefix_len, prefix) == 0;
}

static inline std::string hp_tlv_ascii_field(const std::vector<uint8_t> &bytes, size_t value_start, size_t val_size,
                                            size_t field_offset, size_t field_len) {
  if (field_offset >= val_size)
    return std::string();
  const size_t available = std::min(field_len, val_size - field_offset);
  return hp_tlv_value_to_ascii(bytes, value_start + field_offset, available);
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

// Keep the per-device discovery pages focused on tags whose semantics are not
// established yet. Candidate/partially understood tags intentionally remain.
static inline bool hp_tag_has_established_semantics(uint8_t device_id, uint16_t tag) {
  if (device_id == HP_DEV_METER)
    return tag == HP_TAG_GRID_FREQ_PHASE_V || tag == HP_TAG_LINE_VOLTAGE;

  if (device_id == HP_DEV_EMMA)
    return tag == HP_TAG_SERIAL_NUMBER;

  if (device_id != HP_DEV_INVERTER)
    return false;

  switch (tag) {
    case HP_TAG_INVERTER_CAPABILITIES:
    case HP_REG_RATED_POWER:
    case HP_TAG_INVERTER_MODEL_FAMILY:
    case HP_TAG_MONITORING_BOARD_SERIAL:
    case HP_TAG_DC_MBUS_VERSION:
    case HP_REG_PV1_VOLTAGE:
    case HP_REG_PV1_CURRENT:
    case HP_REG_INPUT_POWER:
    case HP_REG_LINE_AB_VOLTAGE:
    case HP_REG_LINE_BC_VOLTAGE:
    case HP_REG_LINE_CA_VOLTAGE:
    case HP_REG_PHASE_A_VOLTAGE:
    case HP_REG_PHASE_B_VOLTAGE:
    case HP_REG_PHASE_C_VOLTAGE:
    case HP_REG_PHASE_B_CURRENT:
    case HP_REG_PHASE_C_CURRENT:
    case HP_REG_ACTIVE_POWER_STANDARD:
    case HP_REG_REACTIVE_POWER_STANDARD:
    case HP_REG_POWER_FACTOR:
    case HP_REG_GRID_FREQUENCY:
    case HP_REG_INTERNAL_TEMPERATURE:
    case HP_REG_INSULATION_RESISTANCE:
    case HP_REG_INVERTER_STATUS:
    case HP_REG_INVERTER_FAULT_CODE:
    case HP_REG_INVERTER_ALARM_1:
    case HP_REG_INVERTER_ALARM_2:
    case HP_REG_INVERTER_ALARM_3:
    case HP_REG_METER_PHASE_A_CURRENT:
    case HP_REG_METER_PHASE_B_CURRENT:
    case HP_REG_METER_PHASE_C_CURRENT:
    case HP_REG_METER_ACTIVE_POWER:
    case HP_REG_METER_REACTIVE_POWER:
    case HP_REG_METER_POWER_FACTOR:
    case HP_REG_METER_PHASE_A_ACTIVE_POWER:
    case HP_REG_METER_PHASE_B_ACTIVE_POWER:
    case HP_REG_METER_PHASE_C_ACTIVE_POWER:
    case HP_REG_STARTUP_TIME:
    case HP_REG_SHUTDOWN_TIME:
    case HP_TAG_LINE_VOLTAGE:
    case HP_TAG_PHASE_VOLTAGE_TLV:
    case HP_TAG_PHASE_VOLTAGE_U32:
    case HP_TAG_PHASE_CURRENT:
    case HP_TAG_LINE_VOLTAGE_U32:
    case HP_TAG_ACTIVE_POWER:
    case HP_TAG_REACTIVE_POWER:
    case HP_TAG_TOTAL_YIELD:
    case HP_TAG_DAILY_ENERGY:
    case HP_TAG_DAILY_DC_ENERGY_YIELD:
    case HP_TAG_MPPT1_TOTAL_DC_ENERGY_YIELD:
    case HP_TAG_BATTERY_CHARGED_TODAY:
    case HP_TAG_BATTERY_CHARGED_TODAY_LEGACY:
    case HP_TAG_BATTERY_DISCHARGED_TODAY:
    case HP_TAG_BATTERY_DISCHARGED_TODAY_LEGACY:
    case HP_TAG_BATTERY_TOTAL_CHARGE:
    case HP_TAG_BATTERY_TOTAL_BLOCK:
    case HP_TAG_BATTERY_TOTAL_DISCHARGE:
    case HP_TAG_BATTERY_RATED_CAPACITY:
    case HP_TAG_BATTERY_POWER:
    case HP_TAG_BATTERY_BUS_VOLTAGE:
    case HP_TAG_BATTERY_SOC:
    case HP_REG_BATTERY_RUNNING_STATUS:
    case HP_REG_BATTERY_WORKING_MODE:
    case HP_TAG_BATTERY_BUS_CURRENT:
    case HP_TAG_BATTERY_INTERNAL_TEMPERATURE:
    case HP_REG_BATTERY_REMAINING_CHARGE_DISCHARGE_TIME:
    case HP_REG_BATTERY_MAXIMUM_CHARGE_POWER:
    case HP_REG_BATTERY_MAXIMUM_DISCHARGE_POWER:
    case HP_REG_BATTERY_END_OF_CHARGE_SOC:
    case HP_REG_BATTERY_END_OF_DISCHARGE_SOC:
    case HP_REG_BATTERY_FORCED_PERIOD:
    case HP_REG_BATTERY_FORCED_POWER:
    case HP_REG_BATTERY_WORKING_MODE_SETTING:
    case HP_REG_BATTERY_CHARGE_FROM_GRID:
    case HP_REG_BATTERY_GRID_CHARGE_CUTOFF_SOC:
    case HP_REG_BATTERY_TOU_PERIODS:
    case HP_TAG_INVERTER_SOLAR_PV:
    case HP_TAG_BUS_CLOCK:
    case HP_TAG_SERIAL_NUMBER:
    case HP_TAG_INVERTER_IDENTIFICATION:
    case HP_TAG_BATTERY_PACK_1_SN_FW:
    case HP_TAG_BATTERY_PACK_2_SN_FW:
    case HP_TAG_BATTERY_PACK_3_SN_FW:
    case HP_TAG_BATTERY_PACK_MODEL:
    case HP_TAG_BATTERY_PACK_MODELS:
    case HP_TAG_BATTERY_PACK_1_HIGHEST_TEMP:
    case HP_TAG_BATTERY_PACK_1_LOWEST_TEMP:
    case HP_TAG_BATTERY_PACK_2_HIGHEST_TEMP:
    case HP_TAG_BATTERY_PACK_2_LOWEST_TEMP:
    case HP_TAG_BATTERY_PACK_3_HIGHEST_TEMP:
    case HP_TAG_BATTERY_PACK_3_LOWEST_TEMP:
      return true;
    default:
      return false;
  }
}

static inline void hp_try_publish_battery_model_values(uint8_t device_id, uint16_t tag,
                                                       const std::vector<uint8_t> &bytes, size_t pos,
                                                       size_t val_size) {
  if (device_id != HP_DEV_INVERTER || pos + 3 + val_size > bytes.size())
    return;

  const size_t value_start = pos + 3;

  if (tag == HP_TAG_BATTERY_PACK_MODEL && val_size > 0) {
    const std::string model = hp_tlv_value_to_ascii(bytes, value_start, val_size);
    if (!model.empty())
      hp_publish_optional_text_sensor("Battery Pack Model", model);
    return;
  }

  if (tag != HP_TAG_BATTERY_PACK_MODELS ||
      val_size < HP_BATTERY_PACK_COUNT * HP_BATTERY_MODULE_MODEL_FIELD_BYTES)
    return;

  const std::string model_1 = hp_tlv_value_to_ascii(bytes, value_start, HP_BATTERY_MODULE_MODEL_FIELD_BYTES);
  const std::string model_2 = hp_tlv_value_to_ascii(
      bytes, value_start + HP_BATTERY_MODULE_MODEL_FIELD_BYTES, HP_BATTERY_MODULE_MODEL_FIELD_BYTES);
  const std::string model_3 = hp_tlv_value_to_ascii(
      bytes, value_start + (2 * HP_BATTERY_MODULE_MODEL_FIELD_BYTES), HP_BATTERY_MODULE_MODEL_FIELD_BYTES);
  if (!model_1.empty())
    hp_publish_optional_text_sensor("Battery Pack 1 Model", model_1);
  if (!model_2.empty())
    hp_publish_optional_text_sensor("Battery Pack 2 Model", model_2);
  if (!model_3.empty())
    hp_publish_optional_text_sensor("Battery Pack 3 Model", model_3);
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

static inline std::string hp_inverter_identification_software(const std::vector<std::string> &runs) {
  if (runs.size() < 3)
    return std::string();
  return runs.back();
}

static inline std::string hp_format_inverter_identification_tlv(const std::vector<uint8_t> &bytes, size_t value_start,
                                                               size_t val_size) {
  std::vector<std::string> runs;
  hp_tlv_ascii_runs(bytes, value_start, val_size, runs);
  if (runs.size() < 2)
    return std::string();

  const std::string &model = runs[0];
  const std::string &serial = runs[1];
  const std::string software = hp_inverter_identification_software(runs);
  char line[320];
  if (!software.empty()) {
    snprintf(line, sizeof(line), "tag=0x%04X inverter_model=%s inverter_serial=%s inverter_software=%s",
             HP_TAG_INVERTER_IDENTIFICATION, model.c_str(), serial.c_str(), software.c_str());
  } else {
    snprintf(line, sizeof(line), "tag=0x%04X inverter_model=%s inverter_serial=%s", HP_TAG_INVERTER_IDENTIFICATION,
             model.c_str(), serial.c_str());
  }
  return line;
}

static inline void hp_try_publish_inverter_identification_value(uint8_t device_id, uint16_t tag,
                                                               const std::vector<uint8_t> &bytes, size_t pos,
                                                               size_t val_size) {
  if (device_id != HP_DEV_INVERTER || tag != HP_TAG_INVERTER_IDENTIFICATION || pos + 3 + val_size > bytes.size())
    return;

  std::vector<std::string> runs;
  hp_tlv_ascii_runs(bytes, pos + 3, val_size, runs);
  if (runs.size() < 2)
    return;

  hp_publish_optional_text_sensor("Inverter Model", runs[0]);
  hp_publish_optional_text_sensor("Inverter Serial Number", runs[1]);
  const std::string software = hp_inverter_identification_software(runs);
  if (!software.empty())
    hp_publish_optional_text_sensor("Inverter Software Version", software);
}

static inline bool hp_is_battery_pack_identity_tag(uint16_t tag) {
  return tag == HP_TAG_BATTERY_PACK_1_SN_FW || tag == HP_TAG_BATTERY_PACK_2_SN_FW ||
         tag == HP_TAG_BATTERY_PACK_3_SN_FW;
}

static inline const char *hp_battery_pack_serial_sensor_name(uint16_t tag) {
  switch (tag) {
    case HP_TAG_BATTERY_PACK_1_SN_FW:
      return "Battery Pack 1 SN";
    case HP_TAG_BATTERY_PACK_2_SN_FW:
      return "Battery Pack 2 SN";
    case HP_TAG_BATTERY_PACK_3_SN_FW:
      return "Battery Pack 3 SN";
    default:
      return nullptr;
  }
}

static inline bool hp_battery_pack_identity_offsets(uint16_t tag, size_t *sn_offset, size_t *fw_offset) {
  switch (tag) {
    case HP_TAG_BATTERY_PACK_1_SN_FW:
      *sn_offset = 0;
      *fw_offset = 20;
      return true;
    case HP_TAG_BATTERY_PACK_2_SN_FW:
      *sn_offset = 18;
      *fw_offset = 38;
      return true;
    case HP_TAG_BATTERY_PACK_3_SN_FW:
      *sn_offset = 18;
      *fw_offset = 38;
      return true;
    default:
      return false;
  }
}

static inline std::string hp_battery_pack_identity_serial(const std::vector<uint8_t> &bytes, size_t value_start,
                                                         size_t val_size, uint16_t tag) {
  size_t sn_offset = 0;
  size_t fw_offset = 0;
  if (!hp_battery_pack_identity_offsets(tag, &sn_offset, &fw_offset))
    return std::string();
  const std::string serial = hp_tlv_ascii_field(bytes, value_start, val_size, sn_offset, 12);
  return hp_string_starts_with(serial, "EX") ? serial : std::string();
}

static inline std::string hp_battery_pack_identity_firmware(const std::vector<uint8_t> &bytes, size_t value_start,
                                                           size_t val_size, uint16_t tag) {
  size_t sn_offset = 0;
  size_t fw_offset = 0;
  if (!hp_battery_pack_identity_offsets(tag, &sn_offset, &fw_offset))
    return std::string();
  const std::string firmware = hp_tlv_ascii_field(bytes, value_start, val_size, fw_offset, 17);
  return hp_string_starts_with(firmware, "V") ? firmware : std::string();
}

static inline std::string hp_format_battery_pack_identity_tlv(uint16_t tag, const std::vector<uint8_t> &bytes,
                                                             size_t value_start, size_t val_size) {
  const std::string serial = hp_battery_pack_identity_serial(bytes, value_start, val_size, tag);
  if (serial.empty())
    return std::string();

  const std::string firmware = hp_battery_pack_identity_firmware(bytes, value_start, val_size, tag);
  char line[320];
  const char *sn_name = hp_battery_pack_serial_sensor_name(tag);
  if (!firmware.empty()) {
    snprintf(line, sizeof(line), "tag=0x%04X %s=%s battery_firmware=%s", tag, sn_name, serial.c_str(),
             firmware.c_str());
  } else {
    snprintf(line, sizeof(line), "tag=0x%04X %s=%s", tag, sn_name, serial.c_str());
  }
  return line;
}

static inline void hp_try_publish_battery_pack_identity_values(uint8_t device_id, uint16_t tag,
                                                              const std::vector<uint8_t> &bytes, size_t pos,
                                                              size_t val_size) {
  if (device_id != HP_DEV_INVERTER || !hp_is_battery_pack_identity_tag(tag) || pos + 3 + val_size > bytes.size())
    return;

  const std::string serial = hp_battery_pack_identity_serial(bytes, pos + 3, val_size, tag);
  if (serial.empty())
    return;

  const char *sn_name = hp_battery_pack_serial_sensor_name(tag);
  if (sn_name != nullptr)
    hp_publish_optional_text_sensor(sn_name, serial);
  const std::string firmware = hp_battery_pack_identity_firmware(bytes, pos + 3, val_size, tag);
  if (!firmware.empty())
    hp_publish_optional_text_sensor("Battery Firmware", firmware);
}

static inline const char *hp_log_only_identity_field_name(uint16_t tag) {
  switch (tag) {
    case HP_TAG_INVERTER_MODEL_FAMILY:
      return "inverter_model_family";
    case HP_TAG_MONITORING_BOARD_SERIAL:
      return "monitoring_board_serial";
    case HP_TAG_DC_MBUS_VERSION:
      return "dc_mbus_version";
    default:
      return nullptr;
  }
}

static inline std::string hp_format_log_only_identity_tlv(const std::vector<uint8_t> &bytes, size_t value_start,
                                                          size_t val_size, uint16_t tag) {
  const char *field_name = hp_log_only_identity_field_name(tag);
  if (field_name == nullptr)
    return std::string();
  const std::string value = hp_tlv_value_to_ascii(bytes, value_start, val_size);
  if (value.empty())
    return std::string();
  char line[192];
  snprintf(line, sizeof(line), "tag=0x%04X %s=%s", tag, field_name, value.c_str());
  return line;
}

static inline std::string hp_current_block_ascii_field(const std::vector<uint8_t> &bytes, size_t value_start,
                                                       size_t val_size, uint16_t block_start, uint8_t word_count,
                                                       uint16_t field_register, size_t field_len) {
  size_t offset = 0;
  const size_t field_words = (field_len + 1) / 2;
  if (!hp_current_block_field_offset(block_start, word_count, field_register, field_words, &offset) ||
      offset + field_len > val_size || value_start + offset + field_len > bytes.size())
    return std::string();
  return hp_tlv_value_to_ascii(bytes, value_start + offset, field_len);
}

static inline void hp_try_publish_battery_pack_identity_registers(uint8_t device_id,
                                                                  const std::vector<uint8_t> &bytes,
                                                                  size_t value_start, size_t val_size,
                                                                  uint16_t block_start, uint8_t word_count) {
  if (device_id != HP_DEV_INVERTER)
    return;

  const uint16_t sn_registers[3] = {0x9538, 0x9562, 0x958C};
  const uint16_t firmware_registers[3] = {0x9542, 0x956C, 0x9596};
  const char *sn_names[3] = {"Battery Pack 1 SN", "Battery Pack 2 SN", "Battery Pack 3 SN"};
  for (size_t pack = 0; pack < HP_BATTERY_PACK_COUNT; pack++) {
    const std::string serial = hp_current_block_ascii_field(bytes, value_start, val_size, block_start, word_count,
                                                            sn_registers[pack], 12);
    if (hp_string_starts_with(serial, "EX"))
      hp_publish_optional_text_sensor(sn_names[pack], serial);

    const std::string firmware = hp_current_block_ascii_field(
        bytes, value_start, val_size, block_start, word_count, firmware_registers[pack], 17);
    if (hp_string_starts_with(firmware, "V"))
      hp_publish_optional_text_sensor("Battery Firmware", firmware);
  }
}

static inline bool hp_is_battery_pack_temperature_tag(uint16_t tag) {
  return tag == HP_TAG_BATTERY_PACK_1_HIGHEST_TEMP || tag == HP_TAG_BATTERY_PACK_1_LOWEST_TEMP ||
         tag == HP_TAG_BATTERY_PACK_2_HIGHEST_TEMP || tag == HP_TAG_BATTERY_PACK_2_LOWEST_TEMP ||
         tag == HP_TAG_BATTERY_PACK_3_HIGHEST_TEMP || tag == HP_TAG_BATTERY_PACK_3_LOWEST_TEMP;
}

static inline const char *hp_battery_pack_temperature_name(uint16_t tag) {
  switch (tag) {
    case HP_TAG_BATTERY_PACK_1_HIGHEST_TEMP:
      return "battery_pack_1_highest_temp";
    case HP_TAG_BATTERY_PACK_1_LOWEST_TEMP:
      return "battery_pack_1_lowest_temp";
    case HP_TAG_BATTERY_PACK_2_HIGHEST_TEMP:
      return "battery_pack_2_highest_temp";
    case HP_TAG_BATTERY_PACK_2_LOWEST_TEMP:
      return "battery_pack_2_lowest_temp";
    case HP_TAG_BATTERY_PACK_3_HIGHEST_TEMP:
      return "battery_pack_3_highest_temp";
    case HP_TAG_BATTERY_PACK_3_LOWEST_TEMP:
      return "battery_pack_3_lowest_temp";
    default:
      return nullptr;
  }
}

static inline void hp_append_current_field(std::string &summary, const char *field) {
  if (!summary.empty())
    summary += " ";
  summary += field;
}

static inline void hp_append_alarm_name(std::string &alarms, bool active, const char *name) {
  if (!active || alarms.size() >= 236)
    return;
  const size_t separator_size = alarms.empty() ? 0 : 2;
  const size_t name_size = strlen(name);
  if (alarms.size() + separator_size + name_size > 236) {
    alarms += " ...";
    return;
  }
  if (!alarms.empty())
    alarms += "; ";
  alarms += name;
}

static inline std::string hp_inverter_alarm_names(uint16_t alarm_1, uint16_t alarm_2, uint16_t alarm_3) {
  std::string alarms;
  hp_append_alarm_name(alarms, alarm_1 & 0x0001, "High String Input Voltage");
  hp_append_alarm_name(alarms, alarm_1 & 0x0002, "DC Arc Fault");
  hp_append_alarm_name(alarms, alarm_1 & 0x0004, "String Reverse Connection");
  hp_append_alarm_name(alarms, alarm_1 & 0x0008, "String Current Backfeed");
  hp_append_alarm_name(alarms, alarm_1 & 0x0010, "Abnormal String Power");
  hp_append_alarm_name(alarms, alarm_1 & 0x0020, "AFCI Self-Check Fail");
  hp_append_alarm_name(alarms, alarm_1 & 0x0040, "Phase Wire Short-Circuited to PE");
  hp_append_alarm_name(alarms, alarm_1 & 0x0080, "Grid Loss");
  hp_append_alarm_name(alarms, alarm_1 & 0x0100, "Grid Undervoltage");
  hp_append_alarm_name(alarms, alarm_1 & 0x0200, "Grid Overvoltage");
  hp_append_alarm_name(alarms, alarm_1 & 0x0400, "Grid Voltage Imbalance");
  hp_append_alarm_name(alarms, alarm_1 & 0x0800, "Grid Overfrequency");
  hp_append_alarm_name(alarms, alarm_1 & 0x1000, "Grid Underfrequency");
  hp_append_alarm_name(alarms, alarm_1 & 0x2000, "Unstable Grid Frequency");
  hp_append_alarm_name(alarms, alarm_1 & 0x4000, "Output Overcurrent");
  hp_append_alarm_name(alarms, alarm_1 & 0x8000, "Output DC Component Overhigh");

  hp_append_alarm_name(alarms, alarm_2 & 0x0001, "Abnormal Residual Current");
  hp_append_alarm_name(alarms, alarm_2 & 0x0002, "Abnormal Grounding");
  hp_append_alarm_name(alarms, alarm_2 & 0x0004, "Low Insulation Resistance");
  hp_append_alarm_name(alarms, alarm_2 & 0x0008, "Overtemperature");
  hp_append_alarm_name(alarms, alarm_2 & 0x0010, "Device Fault");
  hp_append_alarm_name(alarms, alarm_2 & 0x0020, "Upgrade Failed or Version Mismatch");
  hp_append_alarm_name(alarms, alarm_2 & 0x0040, "License Expired");
  hp_append_alarm_name(alarms, alarm_2 & 0x0080, "Faulty Monitoring Unit");
  hp_append_alarm_name(alarms, alarm_2 & 0x0100, "Faulty Power Collector");
  hp_append_alarm_name(alarms, alarm_2 & 0x0200, "Battery Abnormal");
  hp_append_alarm_name(alarms, alarm_2 & 0x0400, "Active Islanding");
  hp_append_alarm_name(alarms, alarm_2 & 0x0800, "Passive Islanding");
  hp_append_alarm_name(alarms, alarm_2 & 0x1000, "Transient AC Overvoltage");
  hp_append_alarm_name(alarms, alarm_2 & 0x2000, "Peripheral Port Short Circuit");
  hp_append_alarm_name(alarms, alarm_2 & 0x4000, "Churn Output Overload");
  hp_append_alarm_name(alarms, alarm_2 & 0x8000, "Abnormal PV Module Configuration");

  hp_append_alarm_name(alarms, alarm_3 & 0x0001, "Optimizer Fault");
  hp_append_alarm_name(alarms, alarm_3 & 0x0002, "Built-in PID Operation Abnormal");
  hp_append_alarm_name(alarms, alarm_3 & 0x0004, "High Input String Voltage to Ground");
  hp_append_alarm_name(alarms, alarm_3 & 0x0008, "External Fan Abnormal");
  hp_append_alarm_name(alarms, alarm_3 & 0x0010, "Battery Reverse Connection");
  hp_append_alarm_name(alarms, alarm_3 & 0x0020, "On-grid/Off-grid Controller Abnormal");
  hp_append_alarm_name(alarms, alarm_3 & 0x0040, "PV String Loss");
  hp_append_alarm_name(alarms, alarm_3 & 0x0080, "Internal Fan Abnormal");
  hp_append_alarm_name(alarms, alarm_3 & 0x0100, "DC Protection Unit Abnormal");
  hp_append_alarm_name(alarms, alarm_3 & 0x0200, "EL Unit Abnormal");
  hp_append_alarm_name(alarms, alarm_3 & 0x0400, "Active Adjustment Instruction Abnormal");
  hp_append_alarm_name(alarms, alarm_3 & 0x0800, "Reactive Adjustment Instruction Abnormal");
  hp_append_alarm_name(alarms, alarm_3 & 0x1000, "CT Wiring Abnormal");
  hp_append_alarm_name(alarms, alarm_3 & 0x2000, "DC Arc Fault ADMC");
  hp_append_alarm_name(alarms, alarm_3 & 0x4000, "DC Switch Abnormal");
  hp_append_alarm_name(alarms, alarm_3 & 0x8000, "Allowable Battery Discharge Capacity Low");
  return alarms.empty() ? std::string("No active alarms") : alarms;
}

static inline const char *hp_battery_running_status_name(uint16_t status) {
  switch (status) {
    case 0: return "Offline";
    case 1: return "Standby";
    case 2: return "Running";
    case 3: return "Fault";
    case 4: return "Sleep mode";
    default: return nullptr;
  }
}

static inline const char *hp_battery_working_mode_name(uint16_t mode) {
  switch (mode) {
    case 0: return "None";
    case 1: return "Forced charge/discharge";
    case 2: return "Time of use (LG RESU)";
    case 3: return "Fixed charge/discharge";
    case 4: return "Maximum self-consumption";
    case 5: return "Fully fed to grid";
    case 6: return "TOU (LUNA2000)";
    case 7: return "Remote scheduling - maximum self-consumption";
    case 8: return "Remote scheduling - fully fed to grid";
    case 9: return "Remote scheduling - TOU";
    case 10: return "AI energy management and scheduling";
    default: return nullptr;
  }
}

static inline const char *hp_battery_working_mode_setting_name(uint16_t mode) {
  switch (mode) {
    case 0: return "Adaptive";
    case 1: return "Fixed charge/discharge";
    case 2: return "Maximum self-consumption";
    case 3: return "Time of use (LG RESU)";
    case 4: return "Fully fed to grid";
    case 5: return "TOU (LUNA2000)";
    default: return nullptr;
  }
}

static inline std::string hp_named_u16(uint16_t value, const char *name) {
  if (name != nullptr)
    return std::string(name);
  char raw[32];
  snprintf(raw, sizeof(raw), "Unknown: 0x%04X (%u)", value, value);
  return std::string(raw);
}

static inline void hp_append_tou_days(std::string &out, uint8_t days) {
  if ((days & 0x7F) == 0x7F) {
    out += "daily";
    return;
  }
  if ((days & 0x7F) == 0) {
    out += "no days";
    return;
  }
  static const char *const names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  bool first = true;
  for (size_t day = 0; day < 7; day++) {
    if ((days & (1U << day)) == 0)
      continue;
    if (!first)
      out += ",";
    out += names[day];
    first = false;
  }
}

static inline std::string hp_format_battery_tou_periods(const std::vector<uint8_t> &bytes, size_t value_start,
                                                        size_t val_size, uint16_t block_start,
                                                        uint8_t word_count) {
  size_t offset = 0;
  if (!hp_current_block_field_offset(block_start, word_count, HP_REG_BATTERY_TOU_PERIODS,
                                     HP_BATTERY_TOU_PERIOD_WORDS, &offset) ||
      offset + (HP_BATTERY_TOU_PERIOD_WORDS * 2) > val_size ||
      value_start + offset + (HP_BATTERY_TOU_PERIOD_WORDS * 2) > bytes.size())
    return std::string();

  const size_t base = value_start + offset;
  const uint16_t raw_count = hp_u16be(bytes[base], bytes[base + 1]);
  const size_t count = std::min<size_t>(raw_count, HP_BATTERY_TOU_MAX_PERIODS);
  std::string out;
  char period[48];
  for (size_t index = 0; index < count; index++) {
    const size_t pos = base + 2 + (index * 6);
    const uint16_t start_minute = hp_u16be(bytes[pos], bytes[pos + 1]);
    const uint16_t end_minute = hp_u16be(bytes[pos + 2], bytes[pos + 3]);
    const uint8_t action = bytes[pos + 4];
    const uint8_t days = bytes[pos + 5];
    if (!out.empty())
      out += "; ";
    snprintf(period, sizeof(period), "%s %02u:%02u-%02u:%02u ",
             action == 0 ? "Charge" : action == 1 ? "Discharge" : "Unknown",
             static_cast<unsigned>(start_minute / 60), static_cast<unsigned>(start_minute % 60),
             static_cast<unsigned>(end_minute / 60), static_cast<unsigned>(end_minute % 60));
    if (out.size() + strlen(period) > 220) {
      out += "...";
      break;
    }
    out += period;
    hp_append_tou_days(out, days);
    if (out.size() > 220) {
      out.resize(220);
      out += "...";
      break;
    }
  }
  if (raw_count > HP_BATTERY_TOU_MAX_PERIODS) {
    if (!out.empty())
      out += "; ";
    out += "invalid period count";
  }
  return out.empty() ? std::string("No TOU periods") : out;
}

static inline const char *hp_inverter_status_name(uint16_t status) {
  switch (status) {
    case 0x0000: return "Standby: initializing";
    case 0x0001: return "Standby: detecting insulation resistance";
    case 0x0002: return "Standby: detecting irradiation";
    case 0x0003: return "Standby: grid detecting";
    case 0x0100: return "Starting";
    case 0x0200: return "Grid connected";
    case 0x0201: return "Grid connected: power limited";
    case 0x0202: return "Grid connected: self-derating";
    case 0x0203: return "Off-grid: running";
    case 0x0300: return "Shutdown: fault";
    case 0x0301: return "Shutdown: command";
    case 0x0302: return "Shutdown: OVGR";
    case 0x0303: return "Shutdown: communication disconnected";
    case 0x0304: return "Shutdown: power limited";
    case 0x0305: return "Shutdown: manual startup required";
    case 0x0306: return "Shutdown: DC switches disconnected";
    case 0x0307: return "Shutdown: rapid cutoff";
    case 0x0308: return "Shutdown: input underpowered";
    case 0x030C: return "Shutdown: ESS end-of-discharge";
    case 0x0401: return "Grid scheduling: cosphi-P curve";
    case 0x0402: return "Grid scheduling: Q-U curve";
    case 0x0403: return "Grid scheduling: PF-U curve";
    case 0x0404: return "Grid scheduling: dry contact";
    case 0x0405: return "Grid scheduling: Q-P curve";
    case 0x0500: return "Spot-check ready";
    case 0x0501: return "Spot-checking";
    case 0x0600: return "Inspecting";
    case 0x0700: return "AFCI self check";
    case 0x0800: return "I-V scanning";
    case 0x0900: return "DC input detection";
    case 0x0A00: return "Running: off-grid charging";
    case 0xA000: return "Standby: no irradiation";
    default: return nullptr;
  }
}

static inline std::string hp_publish_current_inverter_registers(uint8_t device_id,
                                                                const std::vector<uint8_t> &bytes,
                                                                size_t value_start, size_t val_size,
                                                                uint16_t block_start, uint8_t word_count) {
  std::string summary;
  if (device_id != HP_DEV_INVERTER)
    return summary;

  char field[96];
  uint16_t u16 = 0;
  int16_t i16 = 0;
  uint32_t u32 = 0;
  int32_t i32 = 0;

  uint16_t alarm_1 = 0;
  uint16_t alarm_2 = 0;
  uint16_t alarm_3 = 0;
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_INVERTER_ALARM_1, &alarm_1) &&
      hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_INVERTER_ALARM_2, &alarm_2) &&
      hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_INVERTER_ALARM_3, &alarm_3)) {
    id(inverter_alarm_1_raw).publish_state(static_cast<float>(alarm_1));
    id(inverter_alarm_2_raw).publish_state(static_cast<float>(alarm_2));
    id(inverter_alarm_3_raw).publish_state(static_cast<float>(alarm_3));
    const std::string alarms = hp_inverter_alarm_names(alarm_1, alarm_2, alarm_3);
    id(inverter_active_alarms).publish_state(alarms);
    snprintf(field, sizeof(field), "alarms=%04X/%04X/%04X", alarm_1, alarm_2, alarm_3);
    hp_append_current_field(summary, field);
  }

  // The inverter stream mirrors the external power meter's standard register
  // block. Publish these values as power-meter entities even though FC41
  // device 0x0C carries them; phase voltages continue to come from device 0x02.
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_METER_PHASE_A_CURRENT, &i32)) {
    const float value = static_cast<float>(i32) * HP_METER_PHASE_CURRENT_SCALE;
    id(meter_phase_a_current).publish_state(value);
    snprintf(field, sizeof(field), "meter_phase_A_current=%.2fA", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_METER_PHASE_B_CURRENT, &i32)) {
    const float value = static_cast<float>(i32) * HP_METER_PHASE_CURRENT_SCALE;
    id(meter_phase_b_current).publish_state(value);
    snprintf(field, sizeof(field), "meter_phase_B_current=%.2fA", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_METER_PHASE_C_CURRENT, &i32)) {
    const float value = static_cast<float>(i32) * HP_METER_PHASE_CURRENT_SCALE;
    id(meter_phase_c_current).publish_state(value);
    snprintf(field, sizeof(field), "meter_phase_C_current=%.2fA", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_METER_ACTIVE_POWER, &i32)) {
    id(meter_active_power).publish_state(static_cast<float>(i32));
    snprintf(field, sizeof(field), "meter_active_P=%ldW", static_cast<long>(i32));
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_METER_REACTIVE_POWER, &i32)) {
    id(meter_reactive_power).publish_state(static_cast<float>(i32));
    snprintf(field, sizeof(field), "meter_reactive_Q=%ldvar", static_cast<long>(i32));
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_METER_POWER_FACTOR, &i16)) {
    const float value = static_cast<float>(i16) * 0.001f;
    id(meter_power_factor).publish_state(value);
    snprintf(field, sizeof(field), "meter_power_factor=%.3f", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_METER_PHASE_A_ACTIVE_POWER, &i32)) {
    id(meter_phase_a_power).publish_state(static_cast<float>(i32));
    snprintf(field, sizeof(field), "meter_phase_A_P=%ldW", static_cast<long>(i32));
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_METER_PHASE_B_ACTIVE_POWER, &i32)) {
    id(meter_phase_b_power).publish_state(static_cast<float>(i32));
    snprintf(field, sizeof(field), "meter_phase_B_P=%ldW", static_cast<long>(i32));
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_METER_PHASE_C_ACTIVE_POWER, &i32)) {
    id(meter_phase_c_power).publish_state(static_cast<float>(i32));
    snprintf(field, sizeof(field), "meter_phase_C_P=%ldW", static_cast<long>(i32));
    hp_append_current_field(summary, field);
  }

  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count, HP_REG_PV1_VOLTAGE, &u16)) {
    const float value = static_cast<float>(u16) * 0.1f;
    id(inverter_pv1_voltage).publish_state(value);
    snprintf(field, sizeof(field), "pv1_voltage=%.1fV", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count, HP_REG_PV1_CURRENT, &u16)) {
    const float value = static_cast<float>(u16) * 0.01f;
    id(inverter_pv1_current).publish_state(value);
    snprintf(field, sizeof(field), "pv1_current=%.2fA", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count, HP_REG_INPUT_POWER, &i32)) {
    id(inverter_input_power).publish_state(static_cast<float>(i32));
    snprintf(field, sizeof(field), "input_P=%ldW", static_cast<long>(i32));
    hp_append_current_field(summary, field);
  }

  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_LINE_AB_VOLTAGE, &u16)) {
    const float value = static_cast<float>(u16) * HP_LINE_VOLTAGE_SCALE;
    id(inverter_line_ab_voltage).publish_state(value);
    snprintf(field, sizeof(field), "line_AB=%.1fV", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_LINE_BC_VOLTAGE, &u16)) {
    const float value = static_cast<float>(u16) * HP_LINE_VOLTAGE_SCALE;
    id(inverter_line_bc_voltage).publish_state(value);
    snprintf(field, sizeof(field), "line_BC=%.1fV", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_LINE_CA_VOLTAGE, &u16)) {
    const float value = static_cast<float>(u16) * HP_LINE_VOLTAGE_SCALE;
    id(inverter_line_ca_voltage).publish_state(value);
    snprintf(field, sizeof(field), "line_CA=%.1fV", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_PHASE_A_VOLTAGE, &u16)) {
    const float value = static_cast<float>(u16) * HP_PHASE_VOLTAGE_SCALE;
    id(inverter_phase_a_voltage).publish_state(value);
    snprintf(field, sizeof(field), "phase_A_voltage=%.1fV", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_PHASE_B_VOLTAGE, &u16)) {
    const float value = static_cast<float>(u16) * HP_PHASE_VOLTAGE_SCALE;
    id(inverter_phase_b_voltage).publish_state(value);
    snprintf(field, sizeof(field), "phase_B_voltage=%.1fV", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_PHASE_C_VOLTAGE, &u16)) {
    const float value = static_cast<float>(u16) * HP_PHASE_VOLTAGE_SCALE;
    id(inverter_phase_c_voltage).publish_state(value);
    snprintf(field, sizeof(field), "phase_C_voltage=%.1fV", value);
    hp_append_current_field(summary, field);
  }

  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_PHASE_A_CURRENT, &i32)) {
    const float value = static_cast<float>(i32) * HP_PHASE_CURRENT_SCALE;
    id(inverter_phase_a_current).publish_state(value);
    snprintf(field, sizeof(field), "phase_A_current=%.3fA", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_PHASE_B_CURRENT, &i32)) {
    const float value = static_cast<float>(i32) * HP_PHASE_CURRENT_SCALE;
    id(inverter_phase_b_current).publish_state(value);
    snprintf(field, sizeof(field), "phase_B_current=%.3fA", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_PHASE_C_CURRENT, &i32)) {
    const float value = static_cast<float>(i32) * HP_PHASE_CURRENT_SCALE;
    id(inverter_phase_c_current).publish_state(value);
    snprintf(field, sizeof(field), "phase_C_current=%.3fA", value);
    hp_append_current_field(summary, field);
  }

  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_ACTIVE_POWER_STANDARD, &i32)) {
    id(inverter_active_power).publish_state(static_cast<float>(i32));
    snprintf(field, sizeof(field), "active_P=%ldW", static_cast<long>(i32));
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_REACTIVE_POWER_STANDARD, &i32)) {
    id(inverter_reactive_power).publish_state(static_cast<float>(i32));
    snprintf(field, sizeof(field), "reactive_Q=%ldvar", static_cast<long>(i32));
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i16(bytes, value_start, val_size, block_start, word_count, HP_REG_POWER_FACTOR, &i16)) {
    const float value = static_cast<float>(i16) * 0.001f;
    id(inverter_power_factor).publish_state(value);
    snprintf(field, sizeof(field), "power_factor=%.3f", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_GRID_FREQUENCY, &u16)) {
    const float value = static_cast<float>(u16) * HP_GRID_FREQUENCY_SCALE;
    id(inverter_grid_frequency).publish_state(value);
    snprintf(field, sizeof(field), "grid_frequency=%.2fHz", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_INTERNAL_TEMPERATURE, &i16)) {
    const float value = static_cast<float>(i16) * HP_TEMPERATURE_SCALE;
    id(inverter_internal_temperature).publish_state(value);
    snprintf(field, sizeof(field), "internal_temperature=%.1fC", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_INSULATION_RESISTANCE, &u16)) {
    const float value = static_cast<float>(u16) * 0.001f;
    id(inverter_insulation_resistance).publish_state(value);
    snprintf(field, sizeof(field), "insulation=%.3fMOhm", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_INVERTER_STATUS, &u16)) {
    char status[80];
    const char *known_status = hp_inverter_status_name(u16);
    if (known_status != nullptr) {
      snprintf(status, sizeof(status), "%s", known_status);
    } else {
      snprintf(status, sizeof(status), "raw=0x%04X (%u)", u16, u16);
    }
    id(inverter_status).publish_state(status);
    snprintf(field, sizeof(field), "status=%s", status);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_INVERTER_FAULT_CODE, &u16)) {
    id(inverter_fault_code).publish_state(static_cast<float>(u16));
    snprintf(field, sizeof(field), "fault_code=%u", u16);
    hp_append_current_field(summary, field);
  }

  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count, HP_REG_STARTUP_TIME, &u32)) {
    char timestamp[48];
    hp_format_inverter_event_time(u32, timestamp, sizeof(timestamp));
    id(inverter_startup_time).publish_state(timestamp);
    snprintf(field, sizeof(field), "startup=%s", timestamp);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count, HP_REG_SHUTDOWN_TIME, &u32)) {
    char timestamp[48];
    hp_format_inverter_event_time(u32, timestamp, sizeof(timestamp));
    id(inverter_shutdown_time).publish_state(timestamp);
    snprintf(field, sizeof(field), "shutdown=%s", timestamp);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count, HP_TAG_TOTAL_YIELD, &u32)) {
    const float value = static_cast<float>(u32) * HP_ENERGY_YIELD_SCALE;
    id(inverter_total_yield).publish_state(value);
    snprintf(field, sizeof(field), "total_yield=%.2fkWh", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count, HP_TAG_DAILY_ENERGY, &u32)) {
    const float value = static_cast<float>(u32) * HP_ENERGY_YIELD_SCALE;
    id(inverter_daily_energy).publish_state(value);
    snprintf(field, sizeof(field), "daily_energy=%.2fkWh", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count,
                           HP_TAG_DAILY_DC_ENERGY_YIELD, &u32)) {
    const float value = static_cast<float>(u32) * HP_ENERGY_YIELD_SCALE;
    id(inverter_daily_dc_energy_yield).publish_state(value);
    snprintf(field, sizeof(field), "daily_DC_yield=%.2fkWh", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count,
                           HP_TAG_MPPT1_TOTAL_DC_ENERGY_YIELD, &u32)) {
    const float value = static_cast<float>(u32) * HP_ENERGY_YIELD_SCALE;
    id(inverter_mppt1_total_dc_energy_yield).publish_state(value);
    snprintf(field, sizeof(field), "MPPT1_total_DC_yield=%.2fkWh", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count, HP_REG_RATED_POWER, &u32)) {
    id(inverter_rated_power).publish_state(static_cast<float>(u32));
    snprintf(field, sizeof(field), "rated_P=%luW", static_cast<unsigned long>(u32));
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_TAG_INVERTER_SOLAR_PV, &i32)) {
    id(inverter_solar_power_pv1).publish_state(static_cast<float>(i32));
    snprintf(field, sizeof(field), "solar_PV1=%ldW", static_cast<long>(i32));
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           static_cast<uint16_t>(HP_TAG_INVERTER_SOLAR_PV + 2), &i32)) {
    id(inverter_solar_power_pv2).publish_state(static_cast<float>(i32));
    snprintf(field, sizeof(field), "solar_PV2=%ldW", static_cast<long>(i32));
    hp_append_current_field(summary, field);
  }

  return summary;
}

static inline std::string hp_publish_current_battery_registers(uint8_t device_id,
                                                               const std::vector<uint8_t> &bytes,
                                                               size_t value_start, size_t val_size,
                                                               uint16_t block_start, uint8_t word_count) {
  std::string summary;
  if (device_id != HP_DEV_INVERTER)
    return summary;

  char field[80];
  uint16_t u16 = 0;
  int16_t i16 = 0;
  uint32_t u32 = 0;
  int32_t i32 = 0;

  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_BATTERY_RUNNING_STATUS, &u16)) {
    const std::string status = hp_named_u16(u16, hp_battery_running_status_name(u16));
    id(battery_running_status).publish_state(status);
    snprintf(field, sizeof(field), "battery_status=%s", status.c_str());
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_BATTERY_WORKING_MODE, &u16)) {
    const std::string mode = hp_named_u16(u16, hp_battery_working_mode_name(u16));
    id(battery_working_mode).publish_state(mode);
    snprintf(field, sizeof(field), "battery_mode=%s", mode.c_str());
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_BATTERY_MAXIMUM_CHARGE_POWER, &u32)) {
    id(inverter_battery_maximum_charge_power).publish_state(static_cast<float>(u32));
    snprintf(field, sizeof(field), "battery_max_charge=%luW", static_cast<unsigned long>(u32));
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_BATTERY_MAXIMUM_DISCHARGE_POWER, &u32)) {
    id(inverter_battery_maximum_discharge_power).publish_state(static_cast<float>(u32));
    snprintf(field, sizeof(field), "battery_max_discharge=%luW", static_cast<unsigned long>(u32));
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_BATTERY_END_OF_CHARGE_SOC, &u16)) {
    const float value = static_cast<float>(u16) * HP_BATTERY_SOC_SCALE;
    id(inverter_battery_end_of_charge_soc).publish_state(value);
    snprintf(field, sizeof(field), "battery_end_charge_soc=%.1f%%", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_BATTERY_END_OF_DISCHARGE_SOC, &u16)) {
    const float value = static_cast<float>(u16) * HP_BATTERY_SOC_SCALE;
    id(inverter_battery_end_of_discharge_soc).publish_state(value);
    snprintf(field, sizeof(field), "battery_end_discharge_soc=%.1f%%", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_BATTERY_FORCED_PERIOD, &u16)) {
    id(inverter_battery_forced_charge_discharge_period).publish_state(static_cast<float>(u16));
    snprintf(field, sizeof(field), "battery_forced_period=%umin", u16);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_BATTERY_FORCED_POWER, &i32)) {
    id(inverter_battery_forced_charge_discharge_power).publish_state(static_cast<float>(i32));
    snprintf(field, sizeof(field), "battery_forced_power=%ldW", static_cast<long>(i32));
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_BATTERY_WORKING_MODE_SETTING, &u16)) {
    const std::string mode = hp_named_u16(u16, hp_battery_working_mode_setting_name(u16));
    id(battery_working_mode_setting).publish_state(mode);
    snprintf(field, sizeof(field), "battery_mode_setting=%s", mode.c_str());
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_BATTERY_CHARGE_FROM_GRID, &u16)) {
    const char *name = u16 == 0 ? "Disabled" : u16 == 1 ? "Enabled" : nullptr;
    const std::string enabled = hp_named_u16(u16, name);
    id(battery_charge_from_grid).publish_state(enabled);
    snprintf(field, sizeof(field), "battery_charge_from_grid=%s", enabled.c_str());
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                           HP_REG_BATTERY_GRID_CHARGE_CUTOFF_SOC, &u16)) {
    const float value = static_cast<float>(u16) * HP_BATTERY_SOC_SCALE;
    id(inverter_battery_grid_charge_cutoff_soc).publish_state(value);
    snprintf(field, sizeof(field), "battery_grid_cutoff_soc=%.1f%%", value);
    hp_append_current_field(summary, field);
  }
  const std::string tou_periods =
      hp_format_battery_tou_periods(bytes, value_start, val_size, block_start, word_count);
  if (!tou_periods.empty()) {
    id(battery_tou_schedule).publish_state(tou_periods);
    uint16_t period_count = 0;
    hp_current_block_u16(bytes, value_start, val_size, block_start, word_count,
                         HP_REG_BATTERY_TOU_PERIODS, &period_count);
    snprintf(field, sizeof(field), "battery_tou_periods=%u", period_count);
    hp_append_current_field(summary, field);
  }

  if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count, HP_TAG_BATTERY_POWER, &i32)) {
    id(inverter_battery_power).publish_state(static_cast<float>(i32));
    snprintf(field, sizeof(field), "battery_P=%ldW", static_cast<long>(i32));
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count, HP_TAG_BATTERY_BUS_VOLTAGE,
                           &u16)) {
    const float value = static_cast<float>(u16) * HP_BATTERY_VOLTAGE_SCALE;
    id(inverter_battery_bus_voltage).publish_state(value);
    snprintf(field, sizeof(field), "battery_bus=%.1fV", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count, HP_TAG_BATTERY_SOC, &u16)) {
    const float value = static_cast<float>(u16) * HP_BATTERY_SOC_SCALE;
    id(inverter_battery_soc).publish_state(value);
    snprintf(field, sizeof(field), "battery_soc=%.1f%%", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count, HP_TAG_BATTERY_CHARGED_TODAY,
                           &u32)) {
    const float value = static_cast<float>(u32) * HP_ENERGY_YIELD_SCALE;
    id(inverter_battery_charged_today).publish_state(value);
    snprintf(field, sizeof(field), "battery_charged_today=%.2fkWh", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count,
                           HP_TAG_BATTERY_CHARGED_TODAY_LEGACY, &u32)) {
    const float value = static_cast<float>(u32) * HP_ENERGY_YIELD_SCALE;
    id(inverter_battery_charged_today).publish_state(value);
    snprintf(field, sizeof(field), "battery_charged_today_legacy=%.2fkWh", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count, HP_TAG_BATTERY_DISCHARGED_TODAY,
                           &u32)) {
    const float value = static_cast<float>(u32) * HP_ENERGY_YIELD_SCALE;
    id(inverter_battery_discharged_today).publish_state(value);
    snprintf(field, sizeof(field), "battery_discharged_today=%.2fkWh", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count,
                           HP_TAG_BATTERY_DISCHARGED_TODAY_LEGACY, &u32)) {
    const float value = static_cast<float>(u32) * HP_ENERGY_YIELD_SCALE;
    id(inverter_battery_discharged_today).publish_state(value);
    snprintf(field, sizeof(field), "battery_discharged_today_legacy=%.2fkWh", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i16(bytes, value_start, val_size, block_start, word_count, HP_TAG_BATTERY_BUS_CURRENT,
                           &i16)) {
    const float value = static_cast<float>(i16) * HP_BATTERY_CURRENT_SCALE;
    id(inverter_battery_bus_current).publish_state(value);
    snprintf(field, sizeof(field), "battery_bus_current=%.1fA", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_i16(bytes, value_start, val_size, block_start, word_count,
                           HP_TAG_BATTERY_INTERNAL_TEMPERATURE, &i16)) {
    const float value = static_cast<float>(i16) * HP_TEMPERATURE_SCALE;
    id(inverter_battery_internal_temperature).publish_state(value);
    snprintf(field, sizeof(field), "battery_internal_temp=%.1fC", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count, HP_TAG_BATTERY_TOTAL_BLOCK,
                           &u32)) {
    const float value = static_cast<float>(u32) * HP_ENERGY_YIELD_SCALE;
    id(inverter_battery_total_charge).publish_state(value);
    snprintf(field, sizeof(field), "battery_total_charge=%.2fkWh", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count,
                           HP_TAG_BATTERY_TOTAL_DISCHARGE, &u32)) {
    const float value = static_cast<float>(u32) * HP_ENERGY_YIELD_SCALE;
    id(inverter_battery_total_discharge).publish_state(value);
    snprintf(field, sizeof(field), "battery_total_discharge=%.2fkWh", value);
    hp_append_current_field(summary, field);
  }
  if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count, HP_TAG_BATTERY_RATED_CAPACITY,
                           &u32)) {
    const float value = static_cast<float>(u32) * HP_BATTERY_CAPACITY_SCALE;
    id(inverter_battery_rated_capacity).publish_state(value);
    snprintf(field, sizeof(field), "battery_rated_capacity=%.3fkWh", value);
    hp_append_current_field(summary, field);
  }

  const uint16_t pack_soc_registers[3] = {HP_TAG_BATTERY_PACK_1_SOC, HP_TAG_BATTERY_PACK_2_SOC,
                                         HP_TAG_BATTERY_PACK_3_SOC};
  const uint16_t pack_power_registers[3] = {HP_TAG_BATTERY_PACK_1_POWER, HP_TAG_BATTERY_PACK_2_POWER,
                                           HP_TAG_BATTERY_PACK_3_POWER};
  const uint16_t pack_voltage_registers[3] = {HP_TAG_BATTERY_PACK_1_VOLTAGE, HP_TAG_BATTERY_PACK_2_VOLTAGE,
                                             HP_TAG_BATTERY_PACK_3_VOLTAGE};
  const uint16_t pack_energy_registers[3] = {HP_TAG_BATTERY_PACK_1_TOTAL_ENERGY,
                                            HP_TAG_BATTERY_PACK_2_TOTAL_ENERGY,
                                            HP_TAG_BATTERY_PACK_3_TOTAL_ENERGY};
  for (size_t pack = 0; pack < HP_BATTERY_PACK_COUNT; pack++) {
    if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count, pack_soc_registers[pack],
                             &u16)) {
      const float value = static_cast<float>(u16) * HP_BATTERY_SOC_SCALE;
      if (pack == 0)
        id(inverter_battery_pack_1_soc).publish_state(value);
      else if (pack == 1)
        id(inverter_battery_pack_2_soc).publish_state(value);
      else
        id(inverter_battery_pack_3_soc).publish_state(value);
      snprintf(field, sizeof(field), "pack%u_soc=%.1f%%", static_cast<unsigned>(pack + 1), value);
      hp_append_current_field(summary, field);
    }
    if (hp_current_block_i32(bytes, value_start, val_size, block_start, word_count, pack_power_registers[pack],
                             &i32)) {
      if (pack == 0)
        id(inverter_battery_pack_1_power).publish_state(static_cast<float>(i32));
      else if (pack == 1)
        id(inverter_battery_pack_2_power).publish_state(static_cast<float>(i32));
      else
        id(inverter_battery_pack_3_power).publish_state(static_cast<float>(i32));
      snprintf(field, sizeof(field), "pack%u_P=%ldW", static_cast<unsigned>(pack + 1), static_cast<long>(i32));
      hp_append_current_field(summary, field);
    }
    if (hp_current_block_u16(bytes, value_start, val_size, block_start, word_count, pack_voltage_registers[pack],
                             &u16)) {
      const float value = static_cast<float>(u16) * HP_BATTERY_VOLTAGE_SCALE;
      if (pack == 0)
        id(inverter_battery_pack_1_voltage).publish_state(value);
      else if (pack == 1)
        id(inverter_battery_pack_2_voltage).publish_state(value);
      else
        id(inverter_battery_pack_3_voltage).publish_state(value);
      snprintf(field, sizeof(field), "pack%u_voltage=%.1fV", static_cast<unsigned>(pack + 1), value);
      hp_append_current_field(summary, field);
    }
    if (hp_current_block_u32(bytes, value_start, val_size, block_start, word_count, pack_energy_registers[pack],
                             &u32)) {
      const float value = static_cast<float>(u32) * HP_ENERGY_YIELD_SCALE;
      if (pack == 0)
        id(inverter_battery_pack_1_total_energy).publish_state(value);
      else if (pack == 1)
        id(inverter_battery_pack_2_total_energy).publish_state(value);
      else
        id(inverter_battery_pack_3_total_energy).publish_state(value);
      snprintf(field, sizeof(field), "pack%u_total_energy=%.2fkWh", static_cast<unsigned>(pack + 1), value);
      hp_append_current_field(summary, field);
    }
  }

  const uint16_t temperature_registers[6] = {
      HP_TAG_BATTERY_PACK_1_HIGHEST_TEMP, HP_TAG_BATTERY_PACK_1_LOWEST_TEMP,
      HP_TAG_BATTERY_PACK_2_HIGHEST_TEMP, HP_TAG_BATTERY_PACK_2_LOWEST_TEMP,
      HP_TAG_BATTERY_PACK_3_HIGHEST_TEMP, HP_TAG_BATTERY_PACK_3_LOWEST_TEMP};
  for (size_t index = 0; index < 6; index++) {
    if (!hp_current_block_i16(bytes, value_start, val_size, block_start, word_count, temperature_registers[index],
                              &i16))
      continue;
    const float value = static_cast<float>(i16) * HP_TEMPERATURE_SCALE;
    switch (index) {
      case 0: id(inverter_battery_pack_1_highest_temperature).publish_state(value); break;
      case 1: id(inverter_battery_pack_1_lowest_temperature).publish_state(value); break;
      case 2: id(inverter_battery_pack_2_highest_temperature).publish_state(value); break;
      case 3: id(inverter_battery_pack_2_lowest_temperature).publish_state(value); break;
      case 4: id(inverter_battery_pack_3_highest_temperature).publish_state(value); break;
      case 5: id(inverter_battery_pack_3_lowest_temperature).publish_state(value); break;
      default: break;
    }
    snprintf(field, sizeof(field), "%s=%.1fC", hp_battery_pack_temperature_name(temperature_registers[index]),
             value);
    hp_append_current_field(summary, field);
  }
  return summary;
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
    if (hp_is_battery_pack_temperature_tag(tag) && val_size >= 2) {
      const int16_t raw_temp = hp_i16be(bytes[pos + 3], bytes[pos + 4]);
      snprintf(line, sizeof(line), "tag=0x%04X %s=%.1f°C", tag, hp_battery_pack_temperature_name(tag),
               raw_temp * HP_TEMPERATURE_SCALE);
      return line;
    }
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
    if (device_id == HP_DEV_INVERTER && tag == HP_TAG_INVERTER_CANDIDATE_93C5) {
      snprintf(line, sizeof(line), "tag=0x%04X candidate_i32=%ld", tag, static_cast<long>(i32));
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
    if (tag == HP_TAG_DAILY_DC_ENERGY_YIELD && i32 >= 0) {
      snprintf(line, sizeof(line), "tag=0x%04X daily_DC_yield=%.2fkWh", tag,
               i32 * HP_ENERGY_YIELD_SCALE);
      return line;
    }
    if (tag == HP_TAG_MPPT1_TOTAL_DC_ENERGY_YIELD && i32 >= 0) {
      snprintf(line, sizeof(line), "tag=0x%04X MPPT1_total_DC_yield=%.2fkWh", tag,
               i32 * HP_ENERGY_YIELD_SCALE);
      return line;
    }
    if ((tag == HP_TAG_BATTERY_CHARGED_TODAY || tag == HP_TAG_BATTERY_CHARGED_TODAY_LEGACY) &&
        i32 >= 0) {
      snprintf(line, sizeof(line), "tag=0x%04X battery_charged=%.2fkWh", tag, i32 * HP_ENERGY_YIELD_SCALE);
      return line;
    }
    if ((tag == HP_TAG_BATTERY_DISCHARGED_TODAY || tag == HP_TAG_BATTERY_DISCHARGED_TODAY_LEGACY) &&
        i32 >= 0) {
      snprintf(line, sizeof(line), "tag=0x%04X battery_discharged=%.2fkWh", tag, i32 * HP_ENERGY_YIELD_SCALE);
      return line;
    }
    if (tag == HP_TAG_BATTERY_TOTAL_CHARGE && i32 >= 0) {
      snprintf(line, sizeof(line), "tag=0x%04X battery_total_charge=%.2fkWh", tag, i32 * HP_ENERGY_YIELD_SCALE);
      return line;
    }
    if (tag == HP_TAG_BATTERY_TOTAL_DISCHARGE && i32 >= 0) {
      snprintf(line, sizeof(line), "tag=0x%04X battery_total_discharge=%.2fkWh", tag,
               i32 * HP_ENERGY_YIELD_SCALE);
      return line;
    }
    if (tag == HP_TAG_BATTERY_RATED_CAPACITY && i32 >= 0) {
      snprintf(line, sizeof(line), "tag=0x%04X battery_rated_capacity=%.3fkWh", tag,
               i32 * HP_BATTERY_CAPACITY_SCALE);
      return line;
    }
    if (tag == HP_TAG_BATTERY_POWER) {
      snprintf(line, sizeof(line), "tag=0x%04X battery_P=%ldW", tag, static_cast<long>(i32));
      return line;
    }
    if (device_id == HP_DEV_INVERTER && tag == HP_TAG_SECONDARY_ACTIVE_POWER) {
      snprintf(line, sizeof(line), "tag=0x%04X candidate_P=%ldW", tag, static_cast<long>(i32));
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
    if (tag == HP_TAG_PHASE_VOLTAGE_U32) {
      snprintf(line, sizeof(line), "tag=0x%04X phase_V A=%.1f B=%.1f C=%.1f", tag, v1 * HP_PHASE_VOLTAGE_SCALE,
               v2 * HP_PHASE_VOLTAGE_SCALE, v3 * HP_PHASE_VOLTAGE_SCALE);
      return line;
    }
    snprintf(line, sizeof(line), "tag=0x%04X i32[3]=%ld,%ld,%ld", tag, static_cast<long>(v1), static_cast<long>(v2),
             static_cast<long>(v3));
    return line;
  }
  if (type == 0x0A) {
    if (tag == HP_TAG_BATTERY_TOTAL_BLOCK && val_size >= 8 && pos + 3 + val_size <= bytes.size()) {
      const uint32_t charge_raw = hp_u32be(bytes, pos + 3);
      const uint32_t discharge_raw = hp_u32be(bytes, pos + 7);
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

  if (type >= 0x20) {
    const std::string ascii = hp_tlv_value_to_ascii_runs(bytes, pos + 3, val_size);
    if (!ascii.empty()) {
      snprintf(line, sizeof(line), "tag=0x%04X len=%u ascii=%s", tag, type, ascii.c_str());
      return line;
    }
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
  const size_t value_start = pos + 3;
  if (hp_log_only_identity_field_name(tag) != nullptr && val_size > 0) {
    const std::string identity = hp_format_log_only_identity_tlv(bytes, value_start, val_size, tag);
    if (!identity.empty())
      return identity;
  }

  if (tag == HP_TAG_INVERTER_IDENTIFICATION && val_size > 0) {
    const std::string ident = hp_format_inverter_identification_tlv(bytes, value_start, val_size);
    if (!ident.empty())
      return ident;
  }

  if (hp_is_battery_pack_identity_tag(tag) && val_size > 0) {
    const std::string identity = hp_format_battery_pack_identity_tlv(tag, bytes, value_start, val_size);
    if (!identity.empty())
      return identity;
  }

  if (tag == HP_TAG_BATTERY_PACK_MODEL && val_size > 0) {
    const std::string model = hp_tlv_value_to_ascii(bytes, value_start, val_size);
    const size_t value_end = value_start + val_size;
    size_t metadata_start = value_start + model.size();
    while (metadata_start < value_end && bytes[metadata_start] == 0)
      metadata_start++;
    const size_t metadata_size = value_end - metadata_start;
    const std::string algorithm = hp_tlv_value_to_ascii(bytes, metadata_start, metadata_size);
    std::string key_type;
    if (algorithm.size() + 1 < metadata_size) {
      key_type = hp_tlv_value_to_ascii(bytes, metadata_start + algorithm.size() + 1,
                                       metadata_size - algorithm.size() - 1);
    }
    if (!algorithm.empty() && !key_type.empty()) {
      snprintf(line, sizeof(line), "tag=0x%04X battery_pack_model=%s crypto=%s/%s", tag, model.c_str(),
               algorithm.c_str(), key_type.c_str());
    } else {
      snprintf(line, sizeof(line), "tag=0x%04X battery_pack_model=%s", tag, model.c_str());
    }
    return line;
  }

  if (tag == HP_TAG_BATTERY_PACK_MODELS &&
      val_size >= HP_BATTERY_PACK_COUNT * HP_BATTERY_MODULE_MODEL_FIELD_BYTES) {
    const std::string model_1 = hp_tlv_value_to_ascii(bytes, value_start, HP_BATTERY_MODULE_MODEL_FIELD_BYTES);
    const std::string model_2 = hp_tlv_value_to_ascii(
        bytes, value_start + HP_BATTERY_MODULE_MODEL_FIELD_BYTES, HP_BATTERY_MODULE_MODEL_FIELD_BYTES);
    const std::string model_3 = hp_tlv_value_to_ascii(
        bytes, value_start + (2 * HP_BATTERY_MODULE_MODEL_FIELD_BYTES), HP_BATTERY_MODULE_MODEL_FIELD_BYTES);
    snprintf(line, sizeof(line),
             "tag=0x%04X battery_pack_1_model=%s battery_pack_2_model=%s battery_pack_3_model=%s", tag,
             model_1.c_str(), model_2.c_str(), model_3.c_str());
    return line;
  }

  if (word_count == 1) {
    if (hp_is_battery_pack_temperature_tag(tag)) {
      const int16_t raw_temp = hp_i16be(bytes[pos + 3], bytes[pos + 4]);
      snprintf(line, sizeof(line), "tag=0x%04X %s=%.1f°C", tag, hp_battery_pack_temperature_name(tag),
               raw_temp * HP_TEMPERATURE_SCALE);
      return line;
    }
    const uint16_t word = hp_u16be(bytes[pos + 3], bytes[pos + 4]);
    if (tag == HP_REG_BATTERY_RUNNING_STATUS) {
      const std::string status = hp_named_u16(word, hp_battery_running_status_name(word));
      snprintf(line, sizeof(line), "tag=0x%04X battery_status=%s", tag, status.c_str());
      return line;
    }
    if (tag == HP_REG_BATTERY_WORKING_MODE) {
      const std::string mode = hp_named_u16(word, hp_battery_working_mode_name(word));
      snprintf(line, sizeof(line), "tag=0x%04X battery_mode=%s", tag, mode.c_str());
      return line;
    }
    if (tag == HP_REG_BATTERY_WORKING_MODE_SETTING) {
      const std::string mode = hp_named_u16(word, hp_battery_working_mode_setting_name(word));
      snprintf(line, sizeof(line), "tag=0x%04X battery_mode_setting=%s", tag, mode.c_str());
      return line;
    }
    if (tag == HP_REG_BATTERY_CHARGE_FROM_GRID) {
      snprintf(line, sizeof(line), "tag=0x%04X battery_charge_from_grid=%s", tag,
               word == 0 ? "Disabled" : word == 1 ? "Enabled" : "Unknown");
      return line;
    }
    if (tag == HP_REG_BATTERY_REMAINING_CHARGE_DISCHARGE_TIME) {
      snprintf(line, sizeof(line), "tag=0x%04X remaining_charge_discharge_time=%umin", tag, word);
      return line;
    }
    snprintf(line, sizeof(line), "tag=0x%04X words=1 value=0x%04X(%u)", tag, word, word);
    return line;
  }

  if (word_count == 2 || word_count == 3 || word_count == 4 || word_count == 6)
    return hp_format_typed_tlv(device_id, bytes, pos, tag, word_count, val_size);

  if ((word_count == 10 && (tag == HP_TAG_BATTERY_TOTAL_BLOCK || tag == HP_TAG_SERIAL_NUMBER)) ||
      (word_count == 12 && (tag == HP_TAG_PHASE_VOLTAGE_U32 || tag == HP_TAG_LINE_VOLTAGE_U32))) {
    const std::string decoded = hp_format_typed_tlv(device_id, bytes, pos, tag, word_count, val_size);
    snprintf(line, sizeof(line), "%s raw=%s", decoded.c_str(), hp_hex_slice(bytes, pos + 3, val_size).c_str());
    return line;
  }

  if (word_count >= 0x20) {
    const std::string ascii = hp_tlv_value_to_ascii_runs(bytes, pos + 3, val_size);
    if (!ascii.empty()) {
      snprintf(line, sizeof(line), "tag=0x%04X words=%u ascii=%s", tag, word_count, ascii.c_str());
      return line;
    }
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
  if (type >= 0x20 && !hp_tlv_inline_length_tag(tag))
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

static inline void hp_try_publish_known_tlv(uint8_t device_id, uint16_t tag, uint8_t type,
                                            const std::vector<uint8_t> &bytes, size_t pos, size_t val_size) {
  if (pos + 3 + val_size > bytes.size())
    return;

  if (tag == HP_TAG_BATTERY_TOTAL_BLOCK && type == 0x0A) {
    if (device_id != HP_DEV_INVERTER || val_size < 8)
      return;
    const uint32_t charge_raw = hp_u32be(bytes, pos + 3);
    const uint32_t discharge_raw = hp_u32be(bytes, pos + 7);
    id(inverter_battery_total_charge).publish_state(static_cast<float>(charge_raw) * HP_ENERGY_YIELD_SCALE);
    id(inverter_battery_total_discharge).publish_state(static_cast<float>(discharge_raw) * HP_ENERGY_YIELD_SCALE);
    if (val_size >= 12) {
      const uint32_t capacity_raw = hp_u32be(bytes, pos + 11);
      id(inverter_battery_rated_capacity).publish_state(static_cast<float>(capacity_raw) * HP_BATTERY_CAPACITY_SCALE);
    }
    return;
  }

  if (tag == HP_TAG_INVERTER_SOLAR_PV && type == 0x04) {
    if (device_id != HP_DEV_INVERTER || val_size < 8)
      return;
    id(inverter_solar_power_pv1).publish_state(static_cast<float>(hp_i32be(bytes, pos + 3)));
    id(inverter_solar_power_pv2).publish_state(static_cast<float>(hp_i32be(bytes, pos + 7)));
    return;
  }


  if (tag == HP_TAG_INVERTER_IDENTIFICATION && type >= 0x20) {
    hp_try_publish_inverter_identification_value(device_id, tag, bytes, pos, val_size);
    return;
  }

  if (hp_is_battery_pack_identity_tag(tag) && type >= 0x20) {
    hp_try_publish_battery_pack_identity_values(device_id, tag, bytes, pos, val_size);
    return;
  }

  if (type != 0x02 && type != 0x03 && type != 0x04 && type != 0x06 && type != 0x0C)
    return;

  if (tag == HP_TAG_ACTIVE_POWER && type == 0x02) {
    const float watts = static_cast<float>(hp_i32be(bytes, pos + 3));
    switch (device_id) {
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
      case HP_DEV_INVERTER:
        id(inverter_reactive_power).publish_state(q_var);
        hp_update_power_factor(device_id, false, 0.0f, true, q_var);
        break;
      default:
        break;
    }
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
  if (tag == HP_TAG_DAILY_DC_ENERGY_YIELD && type == 0x02) {
    hp_publish_daily_dc_energy_yield(device_id, hp_u32be(bytes, pos + 3));
    return;
  }
  if (tag == HP_TAG_MPPT1_TOTAL_DC_ENERGY_YIELD && type == 0x02) {
    hp_publish_mppt1_total_dc_energy_yield(device_id, hp_u32be(bytes, pos + 3));
    return;
  }
  if ((tag == HP_TAG_BATTERY_CHARGED_TODAY || tag == HP_TAG_BATTERY_CHARGED_TODAY_LEGACY) &&
      type == 0x02) {
    if (device_id != HP_DEV_INVERTER)
      return;
    const float kwh = static_cast<float>(hp_u32be(bytes, pos + 3)) * HP_ENERGY_YIELD_SCALE;
    id(inverter_battery_charged_today).publish_state(kwh);
    return;
  }
  if ((tag == HP_TAG_BATTERY_DISCHARGED_TODAY || tag == HP_TAG_BATTERY_DISCHARGED_TODAY_LEGACY) &&
      type == 0x02) {
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
  if (tag == HP_TAG_BATTERY_TOTAL_DISCHARGE && type == 0x02) {
    if (device_id != HP_DEV_INVERTER)
      return;
    const float kwh = static_cast<float>(hp_u32be(bytes, pos + 3)) * HP_ENERGY_YIELD_SCALE;
    id(inverter_battery_total_discharge).publish_state(kwh);
    return;
  }
  if (tag == HP_TAG_BATTERY_RATED_CAPACITY && type == 0x02) {
    if (device_id != HP_DEV_INVERTER)
      return;
    const float kwh = static_cast<float>(hp_u32be(bytes, pos + 3)) * HP_BATTERY_CAPACITY_SCALE;
    id(inverter_battery_rated_capacity).publish_state(kwh);
    return;
  }
  if (tag == HP_TAG_PHASE_CURRENT && type == 0x06) {
    const int32_t currents[3] = {hp_i32be(bytes, pos + 3), hp_i32be(bytes, pos + 7), hp_i32be(bytes, pos + 11)};
    hp_publish_phase_current_sensors(device_id, currents);
    return;
  }
  uint16_t voltages[3]{};
  if (tag == HP_TAG_PHASE_VOLTAGE_TLV && type == 0x03) {
    voltages[0] = static_cast<uint16_t>(hp_i16be(bytes[pos + 3], bytes[pos + 4]));
    voltages[1] = static_cast<uint16_t>(hp_i16be(bytes[pos + 5], bytes[pos + 6]));
    voltages[2] = static_cast<uint16_t>(hp_i16be(bytes[pos + 7], bytes[pos + 8]));
    hp_publish_phase_voltage_sensors(device_id, voltages);
    return;
  }
  if (tag == HP_TAG_PHASE_VOLTAGE_U32 && (type == 0x0C || type == 0x06)) {
    const uint32_t u32_voltages[3] = {hp_u32be(bytes, pos + 3), hp_u32be(bytes, pos + 7),
                                      hp_u32be(bytes, pos + 11)};
    hp_publish_phase_voltage_sensors(device_id, u32_voltages);
    return;
  }
  if (tag == HP_TAG_LINE_VOLTAGE_U32 && type == 0x0C) {
    const uint32_t line_v[3] = {hp_u32be(bytes, pos + 3), hp_u32be(bytes, pos + 7),
                                hp_u32be(bytes, pos + 11)};
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

    std::string entry = hp_format_current_data_tlv(device_id, bytes, pos, tag, word_count, val_size);
    std::string decoded_fields = hp_publish_current_inverter_registers(
        device_id, bytes, pos + 3, val_size, tag, word_count);
    const std::string battery_fields = hp_publish_current_battery_registers(
        device_id, bytes, pos + 3, val_size, tag, word_count);
    if (!battery_fields.empty()) {
      if (!decoded_fields.empty())
        decoded_fields += " ";
      decoded_fields += battery_fields;
    }
    if (!decoded_fields.empty()) {
      entry += " decoded={";
      entry += decoded_fields;
      entry += "}";
    }
    entries.push_back(entry);
    hp_note_tag_observation(device_id, tag, entry, hp_hex_slice(bytes, pos + 3, std::min(val_size, size_t{6})));

    hp_try_publish_bus_clock_value(device_id, tag, bytes, pos, val_size);
    hp_try_publish_serial_number_value(device_id, tag, bytes, pos, val_size);
    hp_try_publish_battery_model_values(device_id, tag, bytes, pos, val_size);
    hp_try_publish_battery_pack_identity_registers(device_id, bytes, pos + 3, val_size, tag, word_count);
    hp_try_publish_known_tlv(device_id, tag, word_count, bytes, pos, val_size);

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
    const size_t value_header = is_typed ? 3 : 4;
    hp_note_tag_observation(device_id, tag, entry,
                            hp_hex_slice(bytes, pos + value_header, std::min(advance - value_header, size_t{6})));
    if (is_typed && bytes[pos + 2] == 0x0A) {
      hp_try_publish_bus_clock(device_id, tag, 0x0A, bytes, pos);
    }
    if (is_typed)
      hp_try_publish_serial_number(device_id, tag, bytes[pos + 2], bytes, pos);

    if (publish_phase) {
      if (is_typed) {
        hp_try_publish_known_tlv(device_id, tag, bytes[pos + 2], bytes, pos, advance - 3);
      } else if (tag == HP_TAG_LINE_VOLTAGE || tag == HP_TAG_METER_PHASE_VOLTAGE_AUX ||
                 tag == HP_TAG_GRID_FREQ_PHASE_V || tag == HP_TAG_METER_PHASE_VOLTAGE ||
                 tag == HP_TAG_METER_PHASE_VOLTAGE_ALT) {
        hp_try_publish_count_tag(device_id, tag, bytes, pos);
      }
    }

    if (!tags.empty())
      tags += "; ";
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
  // The high bit is a control flag; the low seven bits carry the number of
  // TLVs. For example, 0x91 describes 17 TLVs and has flag 0x80 set.
  const uint8_t raw_count = bytes[payload_start + 1];
  const uint8_t expected_count = raw_count & 0x7F;
  const uint8_t count_flags = raw_count & 0x80;
  uint8_t parsed_count = 0;
  size_t consumed_end = payload_start + 2;
  hp_walk_current_data_tlvs(bytes, payload_start + 2, payload_end, expected_count, device_id, parsed_count,
                            consumed_end, entries);

  char meta[64];
  if (count_flags != 0) {
    snprintf(meta, sizeof(meta), " seq=0x%02X tlvs=%u flags=0x%02X", sequence, expected_count, count_flags);
  } else {
    snprintf(meta, sizeof(meta), " seq=0x%02X tlvs=%u", sequence, expected_count);
  }
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
                                                size_t payload_end, std::string &tags) {
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

  tags = hp_walk_tlv_payload(bytes, tlv_start, payload_end, true, device_id);
  if (tags.empty()) {
    hp_try_publish_phase_sensors(device_id, bytes, payload_start, payload_end);
  }

  return summary;
}

static inline std::string hp_decode_direct_tag_frame(const std::vector<uint8_t> &bytes, size_t p, size_t payload_start,
                                                     size_t payload_end, std::string &tags) {
  const uint8_t device_id = bytes[p];
  const uint16_t tag = hp_u16be(bytes[p + 2], bytes[p + 3]);

  char head[96];
  snprintf(head, sizeof(head), "dev=%u (0x%02X) fc=0x41 direct-tag=0x%04X", device_id, device_id, tag);
  std::string summary = head;

  tags = hp_walk_tlv_payload(bytes, payload_start, payload_end, true, device_id);

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
  std::string export_tags;
  std::vector<std::string> current_entries;

  if (subfunction == HP_FC41_SUBFN_CURRENT_DATA) {
    summary = hp_decode_0x35_tlv(bytes, p, payload_start, payload_end, current_entries);
  } else if (subfunction == HP_FC41_SUBFN_REPORT) {
    summary = hp_decode_0x89_report(bytes, p, payload_start, payload_end, export_tags);
  } else if (hp_is_count_u16_tag(hp_u16be(subfunction, bytes[p + 3]))) {
    summary = hp_decode_direct_tag_frame(bytes, p, payload_start, payload_end, export_tags);
  } else {
    return {};
  }

  ESP_LOGI(HUAWEI_PROP_TAG, "FC41 %s", summary.c_str());
  const unsigned expected_entries =
      (subfunction == HP_FC41_SUBFN_CURRENT_DATA && payload_end >= payload_start + 2)
          ? static_cast<unsigned>(bytes[payload_start + 1] & 0x7F)
          : static_cast<unsigned>(current_entries.size());
  for (size_t i = 0; i < current_entries.size(); i++) {
    ESP_LOGI(HUAWEI_PROP_TAG, "  TLV %u/%u %s", static_cast<unsigned>(i + 1),
             expected_entries, current_entries[i].c_str());
  }
  hp_publish_dirty_tag_values();
  const bool is_heartbeat =
      subfunction == HP_FC41_SUBFN_CURRENT_DATA && payload_end - payload_start == 1;
  if (!is_heartbeat) {
    if (subfunction == HP_FC41_SUBFN_CURRENT_DATA) {
      for (const std::string &entry : current_entries) {
        if (!export_tags.empty()) {
          export_tags += "; ";
        }
        export_tags += entry;
      }
    }
    id(huawei41_decoded_frame_export).publish_state(summary);
    const std::string raw = hp_hex_slice(bytes, p, bounded_end - p);
    ESP_LOGI(HUAWEI_PROP_FC41_FRAME_TAG, "%s | %s | %s", summary.c_str(),
             export_tags.empty() ? "(none)" : export_tags.c_str(), raw.c_str());
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
  if (hp_uart_pending.empty() && bytes[1] == 0x41 && hp_is_upload_subfunction(bytes[2])) {
    hp_log_opaque_upload_buffer(bytes);
    hp_publish_discovered_device_ids();
    return;
  }

  // Standard Modbus exception response for proprietary function 0x41.
  if (hp_uart_pending.empty() && bytes[1] == HP_FC41_EXCEPTION_FUNCTION) {
    hp_log_exception_buffer(bytes);
    hp_publish_discovered_device_ids();
    return;
  }

  if (hp_uart_pending.size() + bytes.size() > HP_UART_PENDING_MAX) {
    ESP_LOGW(HUAWEI_PROP_TAG, "FC41 RX accumulator overflow (%u + %u bytes); resetting stream",
             static_cast<unsigned>(hp_uart_pending.size()), static_cast<unsigned>(bytes.size()));
    hp_uart_pending.clear();
  }
  hp_uart_pending.insert(hp_uart_pending.end(), bytes.begin(), bytes.end());

  bool found = false;
  size_t p = 0;
  while (p + 4 <= hp_uart_pending.size()) {
    if (hp_uart_pending[p + 1] != 0x41) {
      p++;
      continue;
    }

    const uint8_t subfunction = hp_uart_pending[p + 2];
    if (hp_is_upload_subfunction(subfunction)) {
      std::vector<uint8_t> upload_chunk(hp_uart_pending.begin() + p, hp_uart_pending.end());
      hp_log_opaque_upload_buffer(upload_chunk);
      p = hp_uart_pending.size();
      found = true;
      break;
    }
    const bool direct_tag = hp_is_count_u16_tag(hp_u16be(subfunction, hp_uart_pending[p + 3]));
    if (subfunction != HP_FC41_SUBFN_CURRENT_DATA && subfunction != HP_FC41_SUBFN_REPORT && !direct_tag) {
      p++;
      continue;
    }

    size_t payload_start = 0;
    size_t payload_end = 0;
    size_t frame_end = 0;
    if (subfunction == HP_FC41_SUBFN_CURRENT_DATA) {
      frame_end = p + 4 + hp_uart_pending[p + 3] + 2;
    } else if (subfunction == HP_FC41_SUBFN_REPORT) {
      if (p + 5 > hp_uart_pending.size())
        break;
      frame_end = p + 5 + hp_u16be(hp_uart_pending[p + 3], hp_uart_pending[p + 4]) + 2;
    } else {
      if (p + 6 > hp_uart_pending.size())
        break;
      const uint16_t count = hp_u16be(hp_uart_pending[p + 4], hp_uart_pending[p + 5]);
      if (count == 0 || count > 16) {
        p++;
        continue;
      }
      frame_end = p + 2 + 4 + (2 * count) + 2;
    }

    if (frame_end - p > HP_UART_PENDING_MAX) {
      p++;
      continue;
    }
    if (frame_end > hp_uart_pending.size()) {
      // A valid candidate header at the end of a UART callback is an incomplete
      // frame, not a parse error. Retain it for the next callback.
      break;
    }
    if (!hp_0x41_frame_bounds(hp_uart_pending, p, payload_start, payload_end, frame_end)) {
      p++;
      continue;
    }

    if (!hp_modbus_crc_matches(hp_uart_pending, p, frame_end)) {
      ESP_LOGW(HUAWEI_PROP_TAG, "FC41 CRC mismatch dev=%u (0x%02X) subfn=0x%02X (%s)", hp_uart_pending[p],
               hp_uart_pending[p],
               subfunction, hp_subfunction_name(subfunction));
      p++;
      continue;
    }

    hp_note_device_id(hp_uart_pending[p]);
    hp_decode_0x41_frame(hp_uart_pending, p, frame_end);
    found = true;
    p = frame_end;
  }

  if (p > 0)
    hp_uart_pending.erase(hp_uart_pending.begin(), hp_uart_pending.begin() + p);

  if (found)
    hp_publish_discovered_device_ids();
}

enum class HpModbusFrameKind : uint8_t {
  NONE,
  READ_REQUEST,
  READ_RESPONSE,
  WRITE_SINGLE,
  WRITE_MULTIPLE_REQUEST,
  WRITE_MULTIPLE_RESPONSE,
  EXCEPTION,
};

static inline bool hp_modbus_is_read_function(uint8_t function) {
  return function == 0x01 || function == 0x02 || function == 0x03 || function == 0x04;
}

static inline const char *hp_modbus_function_name(uint8_t function) {
  switch (function) {
    case 0x01: return "read-coils";
    case 0x02: return "read-discrete-inputs";
    case 0x03: return "read-holding-registers";
    case 0x04: return "read-input-registers";
    case 0x05: return "write-single-coil";
    case 0x06: return "write-single-register";
    case 0x0F: return "write-multiple-coils";
    case 0x10: return "write-multiple-registers";
    case 0x16: return "mask-write-register";
    default: return "unknown";
  }
}

static inline void hp_log_generic_modbus_frame(const std::vector<uint8_t> &bytes, size_t start, size_t end,
                                                HpModbusFrameKind kind) {
  const uint8_t device_id = bytes[start];
  const uint8_t function = bytes[start + 1];
  const char *function_name = hp_modbus_function_name(function & 0x7F);
  const std::string raw = hp_hex_slice(bytes, start, end - start);

  if (kind == HpModbusFrameKind::READ_REQUEST) {
    const uint16_t first_register = hp_u16be(bytes[start + 2], bytes[start + 3]);
    const uint16_t register_count = hp_u16be(bytes[start + 4], bytes[start + 5]);
    hp_modbus_last_read_request = {true, device_id, function, first_register, register_count};
    ESP_LOGI(HUAWEI_MODBUS_TAG, "RTU dev=%u fc=0x%02X %s request start=0x%04X(%u) count=%u raw=%s",
             device_id, function, function_name, first_register, first_register, register_count, raw.c_str());
    return;
  }

  if (kind == HpModbusFrameKind::READ_RESPONSE) {
    const uint8_t byte_count = bytes[start + 2];
    const bool correlated = hp_modbus_last_read_request.valid &&
                            hp_modbus_last_read_request.device_id == device_id &&
                            hp_modbus_last_read_request.function == function;
    if (correlated) {
      const uint16_t first_register = hp_modbus_last_read_request.start_register;
      const uint16_t register_count = hp_modbus_last_read_request.register_count;
      std::string known_field;
      if (function >= 0x03 && first_register <= HP_REG_BATTERY_REMAINING_CHARGE_DISCHARGE_TIME &&
          static_cast<uint32_t>(first_register) + register_count >
              HP_REG_BATTERY_REMAINING_CHARGE_DISCHARGE_TIME) {
        const size_t value_offset =
            start + 3 + (2 * (HP_REG_BATTERY_REMAINING_CHARGE_DISCHARGE_TIME - first_register));
        if (value_offset + 1 < end - 2) {
          char field[80];
          snprintf(field, sizeof(field), " remaining_charge_discharge_time=%umin",
                   hp_u16be(bytes[value_offset], bytes[value_offset + 1]));
          known_field = field;
        }
      }
      ESP_LOGI(HUAWEI_MODBUS_TAG,
               "RTU dev=%u fc=0x%02X %s response start=0x%04X(%u) requested=%u data_bytes=%u%s data=%s",
               device_id, function, function_name, first_register, first_register, register_count, byte_count,
               known_field.c_str(), hp_hex_slice(bytes, start + 3, byte_count).c_str());
      hp_modbus_last_read_request.valid = false;
    } else {
      ESP_LOGI(HUAWEI_MODBUS_TAG, "RTU dev=%u fc=0x%02X %s uncorrelated-response data_bytes=%u data=%s raw=%s",
               device_id, function, function_name, byte_count,
               hp_hex_slice(bytes, start + 3, byte_count).c_str(), raw.c_str());
    }
    return;
  }

  if (kind == HpModbusFrameKind::WRITE_SINGLE) {
    const uint16_t address = hp_u16be(bytes[start + 2], bytes[start + 3]);
    const uint16_t value = hp_u16be(bytes[start + 4], bytes[start + 5]);
    ESP_LOGI(HUAWEI_MODBUS_TAG,
             "CONTROL RTU dev=%u fc=0x%02X %s address=0x%04X(%u) value=0x%04X(%u) raw=%s",
             device_id, function, function_name, address, address, value, value, raw.c_str());
    return;
  }

  if (kind == HpModbusFrameKind::WRITE_MULTIPLE_REQUEST) {
    const uint16_t address = hp_u16be(bytes[start + 2], bytes[start + 3]);
    const uint16_t count = hp_u16be(bytes[start + 4], bytes[start + 5]);
    const uint8_t byte_count = bytes[start + 6];
    ESP_LOGI(HUAWEI_MODBUS_TAG,
             "CONTROL RTU dev=%u fc=0x%02X %s request start=0x%04X(%u) count=%u data=%s raw=%s",
             device_id, function, function_name, address, address, count,
             hp_hex_slice(bytes, start + 7, byte_count).c_str(), raw.c_str());
    return;
  }

  if (kind == HpModbusFrameKind::WRITE_MULTIPLE_RESPONSE) {
    const uint16_t address = hp_u16be(bytes[start + 2], bytes[start + 3]);
    const uint16_t count = hp_u16be(bytes[start + 4], bytes[start + 5]);
    ESP_LOGI(HUAWEI_MODBUS_TAG,
             "CONTROL RTU dev=%u fc=0x%02X %s acknowledgement start=0x%04X(%u) count=%u raw=%s",
             device_id, function, function_name, address, address, count, raw.c_str());
    return;
  }

  if (kind == HpModbusFrameKind::EXCEPTION) {
    ESP_LOGW(HUAWEI_MODBUS_TAG, "RTU dev=%u fc=0x%02X %s exception=0x%02X (%s) raw=%s", device_id,
             function, function_name, bytes[start + 2], hp_modbus_exception_name(bytes[start + 2]), raw.c_str());
    return;
  }

  ESP_LOGI(HUAWEI_MODBUS_TAG, "CONTROL RTU dev=%u fc=0x%02X %s raw=%s", device_id, function,
           function_name, raw.c_str());
}

// Passive decoder for ordinary Modbus RTU traffic sharing the same UART. It
// deliberately ignores FC41 frames, validates every standard frame by CRC,
// and correlates read responses with the immediately preceding request so the
// ESPHome log shows which registers EMMA reads or writes.
static inline void huawei_decode_generic_modbus_uart_buffer(const std::vector<uint8_t> &bytes) {
  if (bytes.empty())
    return;
  if (hp_modbus_uart_pending.size() + bytes.size() > HP_MODBUS_UART_PENDING_MAX) {
    ESP_LOGW(HUAWEI_MODBUS_TAG, "RTU RX accumulator overflow (%u + %u bytes); resetting stream",
             static_cast<unsigned>(hp_modbus_uart_pending.size()), static_cast<unsigned>(bytes.size()));
    hp_modbus_uart_pending.clear();
  }
  hp_modbus_uart_pending.insert(hp_modbus_uart_pending.end(), bytes.begin(), bytes.end());

  size_t p = 0;
  while (p + 3 <= hp_modbus_uart_pending.size()) {
    const size_t available = hp_modbus_uart_pending.size() - p;
    const uint8_t function = hp_modbus_uart_pending[p + 1];

    // Proprietary traffic is handled by huawei_decode_proprietary_uart_buffer.
    if (function == 0x41) {
      const uint8_t subfunction = hp_modbus_uart_pending[p + 2];
      if (hp_is_upload_subfunction(subfunction)) {
        p = hp_modbus_uart_pending.size();
        break;
      }
      size_t frame_end = 0;
      if (subfunction == HP_FC41_SUBFN_CURRENT_DATA) {
        if (available < 4)
          break;
        frame_end = p + 4 + hp_modbus_uart_pending[p + 3] + 2;
      } else if (subfunction == HP_FC41_SUBFN_REPORT) {
        if (available < 5)
          break;
        frame_end = p + 5 + hp_u16be(hp_modbus_uart_pending[p + 3], hp_modbus_uart_pending[p + 4]) + 2;
      } else {
        if (available < 6)
          break;
        const uint16_t count = hp_u16be(hp_modbus_uart_pending[p + 4], hp_modbus_uart_pending[p + 5]);
        if (!hp_is_count_u16_tag(hp_u16be(subfunction, hp_modbus_uart_pending[p + 3])) || count == 0 || count > 16) {
          p++;
          continue;
        }
        frame_end = p + 8 + (2 * count);
      }
      if (frame_end > hp_modbus_uart_pending.size())
        break;
      p = frame_end;
      continue;
    }

    if (function == HP_FC41_EXCEPTION_FUNCTION) {
      if (available < 5)
        break;
      p += 5;
      continue;
    }

    HpModbusFrameKind kind = HpModbusFrameKind::NONE;
    size_t frame_end = 0;
    bool incomplete_candidate = false;

    if ((function & 0x80) != 0) {
      frame_end = p + 5;
      if (frame_end <= hp_modbus_uart_pending.size() &&
          hp_modbus_crc_matches(hp_modbus_uart_pending, p, frame_end)) {
        kind = HpModbusFrameKind::EXCEPTION;
      } else if (frame_end > hp_modbus_uart_pending.size()) {
        incomplete_candidate = true;
      }
    } else if (hp_modbus_is_read_function(function)) {
      const size_t request_end = p + 8;
      const uint8_t byte_count = hp_modbus_uart_pending[p + 2];
      const bool response_shape = byte_count <= 250 &&
                                  ((function <= 0x02) || ((byte_count & 1) == 0));
      const size_t response_end = p + 5 + byte_count;
      const bool request_valid = request_end <= hp_modbus_uart_pending.size() &&
                                 hp_modbus_crc_matches(hp_modbus_uart_pending, p, request_end);
      const bool response_valid = response_shape && response_end <= hp_modbus_uart_pending.size() &&
                                  hp_modbus_crc_matches(hp_modbus_uart_pending, p, response_end);
      const bool expecting_response = hp_modbus_last_read_request.valid &&
                                      hp_modbus_last_read_request.device_id == hp_modbus_uart_pending[p] &&
                                      hp_modbus_last_read_request.function == function;
      if (response_valid && (expecting_response || !request_valid)) {
        kind = HpModbusFrameKind::READ_RESPONSE;
        frame_end = response_end;
      } else if (request_valid) {
        kind = HpModbusFrameKind::READ_REQUEST;
        frame_end = request_end;
      } else if (response_valid) {
        kind = HpModbusFrameKind::READ_RESPONSE;
        frame_end = response_end;
      } else if (request_end > hp_modbus_uart_pending.size() ||
                 (response_shape && response_end > hp_modbus_uart_pending.size())) {
        incomplete_candidate = true;
      }
    } else if (function == 0x05 || function == 0x06) {
      frame_end = p + 8;
      if (frame_end <= hp_modbus_uart_pending.size() &&
          hp_modbus_crc_matches(hp_modbus_uart_pending, p, frame_end)) {
        kind = HpModbusFrameKind::WRITE_SINGLE;
      } else if (frame_end > hp_modbus_uart_pending.size()) {
        incomplete_candidate = true;
      }
    } else if (function == 0x0F || function == 0x10) {
      const size_t response_end = p + 8;
      const bool response_valid = response_end <= hp_modbus_uart_pending.size() &&
                                  hp_modbus_crc_matches(hp_modbus_uart_pending, p, response_end);
      bool request_valid = false;
      size_t request_end = 0;
      if (available >= 7 && hp_modbus_uart_pending[p + 6] <= 246) {
        request_end = p + 9 + hp_modbus_uart_pending[p + 6];
        request_valid = request_end <= hp_modbus_uart_pending.size() &&
                        hp_modbus_crc_matches(hp_modbus_uart_pending, p, request_end);
        incomplete_candidate = request_end > hp_modbus_uart_pending.size();
      } else if (available < 7) {
        incomplete_candidate = true;
      }
      if (request_valid) {
        kind = HpModbusFrameKind::WRITE_MULTIPLE_REQUEST;
        frame_end = request_end;
      } else if (response_valid) {
        kind = HpModbusFrameKind::WRITE_MULTIPLE_RESPONSE;
        frame_end = response_end;
      } else if (response_end > hp_modbus_uart_pending.size()) {
        incomplete_candidate = true;
      }
    } else if (function == 0x16) {
      frame_end = p + 10;
      if (frame_end <= hp_modbus_uart_pending.size() &&
          hp_modbus_crc_matches(hp_modbus_uart_pending, p, frame_end)) {
        kind = HpModbusFrameKind::WRITE_SINGLE;
      } else if (frame_end > hp_modbus_uart_pending.size()) {
        incomplete_candidate = true;
      }
    }

    if (kind != HpModbusFrameKind::NONE) {
      hp_log_generic_modbus_frame(hp_modbus_uart_pending, p, frame_end, kind);
      p = frame_end;
      continue;
    }
    if (incomplete_candidate)
      break;
    p++;
  }

  if (p > 0)
    hp_modbus_uart_pending.erase(hp_modbus_uart_pending.begin(), hp_modbus_uart_pending.begin() + p);
}
