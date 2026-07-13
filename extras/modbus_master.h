#pragma once
#include "esphome.h"

static const char *const MODBUS_TX_TAG = "modbus_tx";

static inline uint16_t modbus_crc(const uint8_t *buf, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t pos = 0; pos < len; pos++) {
    crc ^= buf[pos];
    for (int i = 0; i < 8; i++) {
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
  }
  return crc;
}

static inline void send_modbus_read(
    esphome::uart::UARTComponent *uart,
    uint8_t device_id,
    uint16_t address,
    uint16_t count = 15) {

  uint8_t frame[8] = {
    device_id,
    0x03,
    static_cast<uint8_t>((address >> 8) & 0xFF),
    static_cast<uint8_t>(address & 0xFF),
    static_cast<uint8_t>((count >> 8) & 0xFF),
    static_cast<uint8_t>(count & 0xFF),
    0x00,
    0x00
  };

  const uint16_t crc = modbus_crc(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = (crc >> 8) & 0xFF;

  char hexbuf[64];
  snprintf(hexbuf, sizeof(hexbuf),
           "%02X %02X %02X %02X %02X %02X %02X %02X",
           frame[0], frame[1], frame[2], frame[3],
           frame[4], frame[5], frame[6], frame[7]);

  ESP_LOGD(MODBUS_TX_TAG,
           "Sending read: device_id=%u address=%u count=%u frame=%s",
           device_id, address, count, hexbuf);

  uart->write_array(frame, sizeof(frame));
  uart->flush();

  ESP_LOGD(MODBUS_TX_TAG, "Frame sent");
}

static inline void send_modbus_raw_2b(esphome::uart::UARTComponent *uart,
                                      uint8_t device_id,
                                      uint8_t read_dev_id_code,
                                      uint8_t object_id) {
  uint8_t frame[6] = {
    device_id,
    0x2B,
    0x0E,
    read_dev_id_code,
    object_id,
    0x00
  };

  uint16_t crc = modbus_crc(frame, 5);
  frame[5] = crc & 0xFF;

  // Need 7 bytes total, so easier:
  uint8_t full[7] = {
    device_id, 0x2B, 0x0E, read_dev_id_code, object_id,
    static_cast<uint8_t>(crc & 0xFF),
    static_cast<uint8_t>((crc >> 8) & 0xFF)
  };

  ESP_LOGD(MODBUS_TX_TAG,
           "Sending 0x2B request: dev=%u code=0x%02X object=0x%02X frame=%02X %02X %02X %02X %02X %02X %02X",
           device_id, read_dev_id_code, object_id,
           full[0], full[1], full[2], full[3], full[4], full[5], full[6]);

  uart->write_array(full, sizeof(full));
  uart->flush();

  ESP_LOGD(MODBUS_TX_TAG, "0x2B frame sent");
}

static inline void send_modbus_device_identifiers(esphome::uart::UARTComponent *uart,
                                                  uint8_t device_id) {
  send_modbus_raw_2b(uart, device_id, 0x01, 0x00);
}

static inline void send_modbus_device_list(esphome::uart::UARTComponent *uart,
                                           uint8_t device_id) {
  send_modbus_raw_2b(uart, device_id, 0x03, 0x87);
}

static inline std::string mb_ascii(const std::vector<uint8_t> &bytes, size_t start, size_t len) {
  std::string out;
  for (size_t i = 0; i < len && start + i < bytes.size(); i++) {
    char c = static_cast<char>(bytes[start + i]);
    if (c >= 32 && c <= 126) out += c;
  }
  return out;
}


static inline void decode_modbus_2b_response(const std::vector<uint8_t> &bytes) {
  if (bytes.size() < 5) return;

  for (size_t i = 0; i + 5 < bytes.size(); i++) {
    if (bytes[i + 1] != 0x2B || bytes[i + 2] != 0x0E) continue;

    const uint8_t dev = bytes[i];
    const uint8_t read_code = bytes[i + 3];

    ESP_LOGI(MODBUS_TX_TAG,
             "2B response: device_id=%u read_code=0x%02X",
             dev,
             read_code);

    if (read_code == 0x01 && i + 8 < bytes.size()) {
      const uint8_t conformity = bytes[i + 4];
      const uint8_t more = bytes[i + 5];
      const uint8_t next_object = bytes[i + 6];
      const uint8_t object_count = bytes[i + 7];

      ESP_LOGI(MODBUS_TX_TAG,
               "  identifiers: conformity=%u more=%u next=0x%02X objects=%u",
               conformity,
               more,
               next_object,
               object_count);

      size_t p = i + 8;
      for (uint8_t n = 0; n < object_count && p + 2 <= bytes.size(); n++) {
        const uint8_t object_id = bytes[p++];
        const uint8_t len = bytes[p++];

        if (p + len > bytes.size()) {
          ESP_LOGW(MODBUS_TX_TAG, "  object 0x%02X truncated len=%u", object_id, len);
          break;
        }

        const std::string value = mb_ascii(bytes, p, len);

        ESP_LOGI(MODBUS_TX_TAG,
                 "  object 0x%02X len=%u value=\"%s\"",
                 object_id,
                 len,
                 value.c_str());

        p += len;
      }

    } else if (read_code == 0x03 && i + 8 < bytes.size()) {
      const uint8_t conformity = bytes[i + 4];
      const uint8_t more = bytes[i + 5];
      const uint8_t next_object = bytes[i + 6];
      const uint8_t object_count = bytes[i + 7];

      ESP_LOGI(MODBUS_TX_TAG,
               "  device-list: conformity=%u more=%u next=0x%02X objects=%u",
               conformity,
               more,
               next_object,
               object_count);

      size_t p = i + 8;
      for (uint8_t n = 0; n < object_count && p + 2 <= bytes.size(); n++) {
        const uint8_t object_id = bytes[p++];
        const uint8_t len = bytes[p++];

        if (p + len > bytes.size()) {
          ESP_LOGW(MODBUS_TX_TAG, "  list object 0x%02X truncated len=%u", object_id, len);
          break;
        }

        if (object_id == 0x87 && len >= 1) {
          ESP_LOGI(MODBUS_TX_TAG,
                   "  object 0x87 number_of_devices=%u",
                   bytes[p]);
        } else {
          const std::string value = mb_ascii(bytes, p, len);
          ESP_LOGI(MODBUS_TX_TAG,
                   "  object 0x%02X len=%u value=\"%s\"",
                   object_id,
                   len,
                   value.c_str());
        }

        p += len;
      }

    } else {
      ESP_LOGW(MODBUS_TX_TAG,
               "  unsupported 2B read_code=0x%02X",
               read_code);
    }
  }

  for (size_t i = 0; i + 4 < bytes.size(); i++) {
    if (bytes[i + 1] == 0xAB) {
      ESP_LOGW(MODBUS_TX_TAG,
               "2B exception: device_id=%u exception=%u",
               bytes[i],
               bytes[i + 2]);
    }
  }
}