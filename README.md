# Huawei EMMA RS485 TLV Decoder

ESPHome package for passively decoding Huawei EMMA/SUN2000 traffic carried in
proprietary Modbus function `0x41` frames.

The decoder validates the Modbus CRC before publishing data. Current-sensor
sub-function `0x35` is parsed as:

```text
tag       2 bytes
word count 1 byte
payload   word count x 2 bytes
```

File-upload sub-functions `0x05`, `0x06`, and `0x0C` are identified and logged
as opaque frames; their contents are deliberately not decoded. Function `0xC1`
is logged as an abnormal response with its standard Modbus exception code.

UART debug callbacks may split a Modbus frame. The decoder keeps incomplete
bytes in a bounded accumulator and waits for the remainder before checking the
CRC or parsing TLVs. A callback boundary is therefore no longer reported as a
truncated FC41 payload.

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

### Optional RX activity light

The base package calls a no-op RX activity hook for every UART callback, so it
remains portable to boards without an LED. If your local configuration exposes
the onboard LED as a `light` with ID `onboard_led`, include the optional package
file too:

```yaml
packages:
  huawei_emma:
    url: https://github.com/valexi7/Huawei-EMMA-RS485-TLV-Decoder
    ref: main
    files:
      - huawei-emma-rs485.yaml
      - huawei-emma-rx-light.yaml
    refresh: 1d
```

A common GPIO-backed light configuration looks like this; adjust the pin and
inversion for the chosen board:

```yaml
output:
  - platform: gpio
    id: onboard_led_output
    pin:
      number: GPIO2
      inverted: true

light:
  - platform: binary
    id: onboard_led
    output: onboard_led_output
```

Override `huawei_rx_light_id` if your light uses another ID, and
`huawei_rx_light_duration` to change the default 40 ms flash.

### Forwarding decoded frames

The package keeps `Huawei FC41 Decoded Frame` as a diagnostic text sensor and
emits each decoded frame at INFO level with the dedicated logger tag
`huawei_prop_fc41_frame`. The message has three pipe-delimited fields:

```text
<summary> | <semicolon-separated decoded tags, or (none)> | <space-separated raw hex>
```

One-byte current-data heartbeats and opaque upload sub-functions do not emit
this export. All decoded non-heartbeat current-data, report, and direct-tag FC41
frames are exported regardless of device ID. MQTT publishing is not used.

To forward these records through the native ESPHome API, add the following to
the main device YAML. `tx_buffer_size` gives complete frame records enough room
instead of truncating them at the logger's smaller default buffer size.

```yaml
logger:
  tx_buffer_size: 4096
  on_message:
    level: INFO
    then:
      - if:
          condition:
            lambda: |-
              return strcmp(tag, "huawei_prop_fc41_frame") == 0;
          then:
            - homeassistant.event:
                event: esphome.huawei_prop_fc41_frame
                data:
                  message: !lambda 'return message;'
```

This requires the native `api:` component shown in the minimal configuration.
Home Assistant receives an `esphome.huawei_prop_fc41_frame` event whose
`message` field contains the summary, decoded tags, and raw frame hex. The event
can then be consumed by a Home Assistant automation or stored for later use.

## Hardware

Do not connect an ESP GPIO directly to RS485 A/B. Use a 3.3 V-compatible,
preferably isolated RS485 receiver/transceiver:

1. Connect the bus A/B pair to the receiver A/B terminals.
2. Connect the receiver's RO output to `${huawei_uart_rx_pin}`.
3. Hold the receiver/transceiver in receive-only mode. The supplied base
   package never transmits and does not configure board-specific DE, RE,
   power-enable, or LED pins.
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
| `huawei_tlv_refresh` | `0s` | External-component refresh interval; always refresh avoids package/component version skew |

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

- `huawei-emma-rs485.yaml` - reusable ESPHome sensors and UART decoder package.
- `huawei-emma-rx-light.yaml` - optional RX activity light hook.
- `components/huawei_emma_tlv/` - Git-backed parser component.
- `esp32.yaml` - deliberately minimal device example.
- `extras/` - inactive Modbus polling/reference helpers retained from the
  original work; they are not loaded by the TLV package.

The package exposes discovered-tag text sensors for known device IDs 0, 2, and
12, plus a shared-tag sensor. The inverter catalog has ten pages; the EMMA and
power-meter catalogs have five pages each. Each entry includes its compact
latest raw value, such as `7D5F=00000382`. Tags belonging to other device IDs are grouped
on additional pages, for example `0x80: 7530=value, 7540=value`. A tag seen
under multiple device IDs is logged once per device with its decoded sample,
making possible cross-device meanings visible without flooding the log.
Tags with established semantics are omitted from the per-device discovered
catalogs so those pages stay focused on reverse-engineering unknown fields.
Their decoded values remain available through entities and frame logs. `Unknown
FC41 Device Tags` contains traffic from unrecognized device IDs. Unknown tags do
not create guessed numeric entities.

Battery model tag `0x9640` exposes its leading null-terminated, zero-padded
string as `Battery Pack Model`; its remaining bytes are crypto metadata rather
than part of the model name. Tag `0x985B` is split into three 30-byte fields exposed as
`Battery Pack 1 Model`, `Battery Pack 2 Model`, and `Battery Pack 3 Model`.
Battery identity tags `0x9538`, `0x9559`, and `0x9583` publish `Battery Pack 1 SN`,
`Battery Pack 2 SN`, and `Battery Pack 3 SN`; the shared firmware run is published as
`Battery Firmware` when present. Battery temperature tags `0x9634` through `0x9639` are
scaled as 0.1 °C values.
Inverter identification tag `0x7530` publishes `Inverter Model`, `Inverter Serial Number`,
and `Inverter Software Version`. Long current-data TLVs with other printable content are
rendered as semicolon-separated ASCII runs in frame logs.
Current-data TLV counts use the low seven bits of the count byte; the high bit
is retained as a frame flag in decoder summaries.

In the July 2026 capture used for this revision, device 2 had 16 tags and device
12 had 52 tags, with no tag ID present in both sets. Known decoding is therefore
device-scoped. In particular, meter tags `0x7729`, `0x7734`, and `0x9C52` are
retained as voltage-like candidates in logs/discovery but do not overwrite the
phase-voltage sensors; canonical meter phase voltage and frequency currently
come from `0x7733`.
