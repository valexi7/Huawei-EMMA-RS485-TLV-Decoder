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

The package currently keeps `Huawei FC41 Decoded Frame` as the diagnostic text
sensor state and also publishes a JSON payload to MQTT topic
`huawei-emma-rs485/fc41_frame` when the MQTT client is configured and connected.

- `state`: frame summary header
- `tags`: decoded TLV entries for current-data frames
- `raw`: raw hex bytes for the exported frame

One-byte current-data heartbeats and opaque upload sub-functions do not trigger
this export.

### MQTT broker configuration

Add an MQTT section in your local ESPHome device config (not in the package):

```yaml
mqtt:
  broker: 192.168.1.10
  username: !secret mqtt_user
  password: !secret mqtt_password
```

### Listen to FC41 topic

Use `mosquitto_sub` to watch published decoded frames:

```bash
mosquitto_sub -h 192.168.1.10 -u "<user>" -P "<password>" -t "huawei-emma-rs485/fc41_frame" -v
```

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
12, plus a shared-tag sensor. Each entry includes its compact latest raw value,
such as `7D5F=00000382`. Tags belonging to other device IDs are grouped
on additional pages, for example `0x80: 7530=value, 7540=value`. A tag seen
under multiple device IDs is logged once per device with its decoded sample,
making possible cross-device meanings visible without flooding the log.
Unknown tags do not create guessed numeric entities.

In the July 2026 capture used for this revision, device 2 had 16 tags and device
12 had 52 tags, with no tag ID present in both sets. Known decoding is therefore
device-scoped. In particular, meter tags `0x7729`, `0x7734`, and `0x9C52` are
retained as voltage-like candidates in logs/discovery but do not overwrite the
phase-voltage sensors; canonical meter phase voltage and frequency currently
come from `0x7733`.
