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

Ordinary Modbus RTU functions on the same RX stream are also CRC-checked and
logged with the `huawei_modbus` tag. Read responses are correlated with the
preceding request so their starting register is visible. FC05/06/0F/10/16
writes are prefixed with `CONTROL`, making EMMA control traffic easy to filter:

```text
[I][huawei_modbus] RTU dev=12 fc=0x03 read-holding-registers request start=0x90A1(37025) count=1 ...
[I][huawei_modbus] RTU dev=12 fc=0x03 read-holding-registers response start=0x90A1(37025) ... remaining_charge_discharge_time=564min ...
[I][huawei_modbus] CONTROL RTU dev=12 fc=0x10 write-multiple-registers request start=0xB897(47255) ...
```

This is passive logging only; the package never transmits Modbus commands.

<img width="340" height="600" alt="image" src="https://github.com/user-attachments/assets/51cc1595-2606-4c61-9bd0-3767fc68c8fc" /><img width="340" height="600" alt="image" src="https://github.com/user-attachments/assets/2e5d098b-9de5-4e64-bda9-798912a8eca6" />

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

The package keeps `ZZ Reverse Engineering - FC41 Decoded Frame` as a diagnostic
text sensor and emits each decoded frame at INFO level with the dedicated
logger tag `huawei_prop_fc41_frame`. The message has three pipe-delimited
fields:

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

<img width="900" height="660" alt="image" src="https://github.com/user-attachments/assets/d864c259-fe10-4b40-bb69-8895fb6cde6c" />


1. Connect the bus A/B pair to the receiver A/B terminals.
2. Connect the receiver's RO output to `${huawei_uart_rx_pin}`.
3. Hold the receiver/transceiver in receive-only mode. The supplied base
   package never transmits and does not configure board-specific DE, RE,
   power-enable, or LED pins.
4. Add any enable pins required by your chosen board to your local YAML.
5. From EMMA terminal block you can get +12 V

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
- `tools/analyze_huawei_csv.py` - dependency-free CSV replay and register-analysis tool.
- `esp32.yaml` - deliberately minimal device example.
- `extras/` - inactive Modbus polling/reference helpers retained from the
  original work; they are not loaded by the TLV package.

Analyze a logger export and optionally retain the complete machine-readable
result:

```bash
python tools/analyze_huawei_csv.py huawei-rs485-logger.csv \
  --json huawei-rs485-analysis.json
```

The analyzer validates CRCs and `0x35` block alignment, prints the latest known
battery/inverter values, and ranks three-value register candidates against
inverter active power. It accepts raw hexadecimal frame cells, JSON logger
exports containing a `raw` field, and the logger's `{summary=..., raw=...}`
cell format. Date-only exports use the inverter bus clock as their timeline
anchor instead of discarding otherwise valid rows.

The inverter device stream also publishes the decoded battery configuration
and alarm fields observed in FC41 current-data blocks:

- battery running status, live working mode, configured working mode, and
  charge-from-grid state;
- maximum charge/discharge power, end-of-charge/end-of-discharge SOC, grid
  charge cutoff SOC, and forced charge/discharge period and power;
- external power-meter phase currents, total active/reactive power, power
  factor, and per-phase active power from the inverter's mirrored meter block;
- current-day DC energy yield and cumulative MPPT1 DC energy yield from
  registers `0x7D80` and `0x7DD4`;
- LUNA2000 TOU periods from register block `0xB897`, formatted as readable
  charge/discharge time ranges and active weekdays;
- inverter fault code, three raw alarm words, and a combined active-alarm text
  sensor with decoded alarm names.

The live working-mode sensor distinguishes maximum self-consumption from local
or remote-scheduled TOU modes when those register windows are broadcast. The
TOU schedule entity remains unchanged until the complete 43-register `0xB897`
block appears on the bus.

The `ZZ Reverse Engineering` diagnostics expose discovered-tag text sensors
for known device IDs 0, 2, and 12, plus a shared-tag sensor. The inverter
catalog has ten pages; the EMMA and power-meter catalogs have five pages each.
Each entry includes its compact latest raw value, such as `7D5F=00000382`. Tags
belonging to other device IDs are grouped on additional pages, for example
`0x80: 7530=value, 7540=value`. A tag seen under multiple device IDs is logged
once per device with its decoded sample, making possible cross-device meanings
visible without flooding the log.
EMMA entities are currently marked `internal` because recent captures have not
contained sustained online EMMA traffic.
Tags with established semantics are omitted from the per-device discovered
catalogs so those pages stay focused on reverse-engineering unknown fields.
Their decoded values remain available through entities and frame logs. `Unknown
FC41 Device Tags` contains traffic from unrecognized device IDs. Unknown tags do
not create guessed numeric entities.

## Motivation

When Huawei EMMA is connected to an inverter, it takes control of the system.
EMMA communicates with the inverter over Modbus, but most of this communication
does not use the standard Modbus function codes.

Adding a second Modbus master to poll values from the system can work, but the
additional traffic may eventually overload the bus. This can delay responses
and potentially disrupt communication between EMMA and the inverter.

Huawei uses the proprietary, user-defined Modbus function code `0x41` for this
communication. Huawei's documentation describes `0x41` as a file-transfer
function, which this repository also recognizes, but the protocol carries much
more than file transfers. Sub-function `0x89` is used for reports, while
sub-function `0x35` carries control and sensor data.

Huawei EMMA RS485 TLV Decoder connects to the same RS485 bus as a passive
listener. It observes this control traffic and decodes the values into Home
Assistant sensors without adding another master or transmitting on the bus.

All sensor tags and payload formats supported by this project have been
reverse-engineered from captured traffic. No public documentation is currently
available for Huawei's proprietary use of the user-defined function code
`0x41`.
