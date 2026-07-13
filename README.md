# Huawei EMMA RS485 TLV Decoder

ESPHome package for passively decoding Huawei EMMA/SUN2000 traffic carried in
proprietary Modbus function `0x41` frames.

The decoder validates the Modbus CRC before publishing data. Current-sensor
sub-function `0x35` is parsed as:

```text
tag       2 bytes
word count 1 byte
payload   word count × 2 bytes
```

File-upload sub-functions `0x05`, `0x06`, and `0x0C` are identified and logged
as opaque frames; their contents are deliberately not decoded. Function `0xC1`
is logged as an abnormal response with its standard Modbus exception code.

## Minimal ESP32 configuration

Copy [`esp32.yaml`](esp32.yaml) into your ESPHome configuration directory and
change the board and RX pin for your hardware. The package itself does not
select a board, Wi-Fi settings, API credentials, or OTA credentials.

```yaml
substitutions:
  name: huawei-emma-rs485
  friendly_name: Huawei EMMA RS485
  board: esp32dev
  huawei_uart_rx_pin: GPIO21

packages:
  huawei_emma: github://valexi7/Huawei-EMMA-RS485-TLV-Decoder/huawei-emma-rs485.yaml@main

esphome:
  name: ${name}
  friendly_name: ${friendly_name}

esp32:
  board: ${board}
  framework:
    type: esp-idf

logger:
api:
ota:
  - platform: esphome

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
```

ESPHome uses `!include` for local files. A GitHub-hosted include uses the
[`packages` remote-Git syntax](https://esphome.io/components/packages/) shown
above; `!include` cannot fetch an HTTP or GitHub URL directly.

For an explicit update interval, use the expanded equivalent:

```yaml
packages:
  huawei_emma:
    url: https://github.com/valexi7/Huawei-EMMA-RS485-TLV-Decoder
    ref: main
    files:
      - huawei-emma-rs485.yaml
    refresh: 1d
```

`main` follows current development. ESPHome refreshes the Git checkout during
configuration validation after the refresh interval. Recompile and install the
firmware to put an updated decoder on the device; a running device does not
self-update merely because the repository changed. Pin `ref` to a release tag
or commit when reproducible builds are more important than automatic tracking.

## Hardware

Do not connect an ESP GPIO directly to RS485 A/B. Use a 3.3 V-compatible,
preferably isolated RS485 receiver/transceiver:

1. Connect the bus A/B pair to the receiver A/B terminals.
2. Connect the receiver's RO output to `${huawei_uart_rx_pin}`.
3. Hold the receiver/transceiver in receive-only mode. The supplied package
   never transmits and does not configure board-specific DE, RE, power-enable,
   or status-LED pins.
4. Add any enable pins required by your chosen board to your local YAML.

The defaults are 9600 baud, 8 data bits, no parity, and one stop bit. Change
these substitutions in the local file when necessary:

| Substitution | Default | Purpose |
| --- | --- | --- |
| `huawei_uart_rx_pin` | `GPIO21` | Receiver output pin |
| `huawei_uart_baud_rate` | `9600` | Bus baud rate |
| `huawei_uart_rx_buffer_size` | `512` | ESPHome UART receive buffer |
| `huawei_log_level` | `INFO` | Decoder log level |
| `huawei_tlv_ref` | `main` | External-component Git ref |
| `huawei_tlv_refresh` | `1d` | External-component refresh interval |

## Local development

After cloning this repository beside your test configuration, a local package
can be loaded with current ESPHome releases as a list entry:

```yaml
packages:
  - !include huawei-emma-rs485.yaml
```

The package still obtains the external component from GitHub. For fully local
component development, change that package's `external_components` source to
the repository's local `components` directory while testing.

## Repository layout

- `huawei-emma-rs485.yaml` — reusable ESPHome sensors and UART decoder package.
- `components/huawei_emma_tlv/` — Git-backed parser component.
- `esp32.yaml` — deliberately minimal device example.
- `extras/` — inactive Modbus polling/reference helpers retained from the
  original work; they are not loaded by the TLV package.

Unknown tags are logged for reverse-engineering but do not create diagnostic
entities. This avoids publishing guessed or nonsensical values to Home
Assistant.
