# EcoWatch Julia — hardware notes

Device: **EcoWatch Julia** by Waltrix AB (www.waltrix.se), Made in Sweden.
An electricity-spot-price wall clock: an analog clock with RGB LED rings that colour each hour
of the day green / yellow / red by price. Label: "1xAA BATTERY + USB 5V/2A", batch `202502`.
PCB revision: Waltrix 3.0.1.

Photos: [pcb-esp32-closeup.jpg](img/pcb-esp32-closeup.jpg), [pcb-overview.jpg](img/pcb-overview.jpg)

## From the vendor manual (2026-03-24) and support page

- **The AA battery only runs the analog quartz movement** (hands are set with the knob on the
  back). The ESP32 does not drive the hands. → Nothing to replicate there.
- **USB 5 V/2 A** powers the ESP32 and LEDs, normal consumption 1.5 W. In the box there is only a
  USB-A to barrel-jack cord (barrel end at the clock), no power plug.
- **Outer ring:** 12–72 LEDs depending on model (1–6 per hour); shows 24 h of prices, supports
  15-minute resolution → Julia probably has ≥ 48 outer LEDs (4 per hour). **Count them.**
- **Inner ring:** 12–24 LEDs (Ida and Julia only), used for tariff / peak-load hours, live power
  from a HomeWizard P1 meter, and "flash current price".
- Boot sequence red → white → green, then green spinning while waiting for setup.
- Factory reset: press RST five times, a few seconds apart (multi-reset detector in RTC
  memory / NVS). Rear label says "Reset ×2".
- Setup AP `EcoWatch_xxxx` / `EcoWatch-Julia-xxxx`, portal at `192.168.4.1`, called a
  "Configuration Portal" (WiFiManager wording). After setup: `http://ecowatch.local`,
  `http://ecowatch.local/discovery` (fw ≥ 3.4.6).
- Web UI pages: Basic, Advanced, Devices (Shelly plugs, HomeWizard P1), API (MQTT, URL POST),
  Update (shows version, **"Get latest firmware" downloads the vendor .bin**, manual .bin upload).
- MQTT: publishes `energywatch/zone` hourly as JSON `{zone: 1|2|3, price}`; listens on
  `energywatch/update` and `energywatch/full`; also accepts JSON to drive rings per hour or per
  LED (separate API manual, not public as far as found). Old product name: *EnergyWatch*.
- Current firmware (support page, Nov 2026): EcoWatch **3.6.4**.

## From the stock flash dump (2026-11-08, `dumps/ecowatch-julia-original-20261108-160051.bin`)

- Chip: ESP32-D0WDQ6 rev 1.1, 40 MHz crystal, **4 MB flash** (manufacturer 0x5E, device 0x4016),
  flash voltage 3.3 V.
- Framework: **Arduino-ESP32 2.0.x (ESP-IDF v4.4.7) + tzapu WiFiManager**. Not Tasmota: the
  Tasmota feel comes from WiFiManager's web UI, which uses the same look.
- LEDs are driven through the legacy **RMT** driver (`rmt_config`, `rmt_set_gpio`), so they are
  WS2812-style addressable LEDs. No FastLED/NeoPixel library names, so it is probably a hand-rolled
  RMT driver. The pin number is not visible as a string.
- Partition table (like Arduino's `min_spiffs`):

  | label | type | offset | size |
  |-------|------|--------|------|
  | nvs | data/nvs | 0x9000 | 0x5000 |
  | otadata | data/ota | 0xE000 | 0x2000 |
  | app0 | ota_0 | 0x10000 | 0x1C0000 — fw 3.4.3 (previous) |
  | app1 | ota_1 | 0x1D0000 | 0x1C0000 — **fw 3.6.4 (active)** |
  | eeprom | data/0x99 | 0x390000 | 0x1000 (empty) |
  | spiffs | data/spiffs | 0x391000 | 0xF000 — actually LittleFS |

- LittleFS files: `config.json` (all UI settings, including the MQTT password in plain text), `device.json`,
  `drd.dat` (double-reset detector), `reset_count` (5-press factory reset).
- The model is chosen in code: hostname/AP `EcoWatch-Julia-<mac>`, and `Julia` / `Johanna` strings
  are present. One firmware runs on all models.
- Cloud endpoints: firmware `http://s3.eu-north-1.amazonaws.com/ecogather/newfirmware/firmware.bin`
  (plain HTTP, public), telemetry `https://api.waltrix.se/telemetry`, setup e-mail via an AWS API
  Gateway endpoint.
- MQTT topics: `ecowatch/zone`, `ecowatch/update`, `ecowatch/ping`, `ecowatch/send/full`,
  `ecowatch/recieve/full` (sic), `ecowatch/{v}/set/pixels`, `ecowatch/{v}/set/hours`,
  `ecowatch/{v}/set/settings`, `ecowatch/{v}/clear`, `ecowatch/{v}/reset`; HA discovery under
  `homeassistant/device/`. (The 2026 manual still says `energywatch/...`, but the firmware uses `ecowatch/...`.)
- Serial log at 115200 is almost silent (only core-dump / LittleFS errors at boot).

## Identified parts

| Ref | Part | Notes |
|-----|------|-------|
| U1 | ESP32-WROOM-32 (Espressif) | Classic dual-core ESP32, likely 4 MB flash. Confirm with `esptool flash_id`. |
| U2 | WCH CH340 (SOIC-16) | USB-UART bridge. Has external crystal X2 → probably CH340G. |
| Q3, Q4 | SOT-23 transistors | Standard DTR/RTS auto-reset/auto-boot circuit → esptool can flash without pressing buttons. |
| BT1 | Push button "BOOT" | GPIO0 to GND. |
| BT2 | Push button "RST" | EN to GND. This is the "reset button" the manual means. |
| SW2 | Slide switch (marked ON) | Function unknown — power switch? **TODO: trace.** |
| D1 | "M1" diode (SMA) | Probably reverse-polarity / USB VBUS OR-ing diode. |
| USB1 | Micro-USB | 5 V input + serial (via CH340). **This is the flashing port.** |
| U4 | SOT-223 regulator near 5V jack | Likely 3.3 V LDO (e.g. AMS1117). **TODO: read marking.** |
| — | 5V / "Bat" pads near bottom | Second power input; "LED7&5" silkscreen nearby. |
| U7 | 8-pin header labelled as an SPI TFT connector | See below — probably not a display on Julia. |
| — | RGB LEDs on the ring under the white toothed light guide | Almost certainly WS2812/SK6812. **TODO: type, count, data GPIO(s).** |

## Header U7

| Pin | Silkscreen | ESP32 GPIO |
|-----|--------|-----------|
| 1 | 3.3V | — |
| 2 | GND | — |
| 3 | CS | GPIO5 |
| 4 | RST | **not labelled** |
| 5 | DC | GPIO16 |
| 6 | MOSI | GPIO23 |
| 7 | SCK | GPIO18 |
| 8 | LED | GPIO22 |

This is the pinout of cheap SPI TFT modules (ST7735 / ST7789 / ILI9341), but the Julia has no
screen — the PCB is likely shared with another model, or this is a spare expansion port.
It does **not** carry UART, so flash over micro-USB instead.
GPIO22 is **not** the LED data line (tested). The LED rings are on GPIO17 and GPIO15.

## Still unknown (fill in as you find out)

- [x] LED counts: outer 48 (4 per hour), inner 24 (2 per hour). Type still unconfirmed (WS2812-style, RMT)
- [x] LED data: **outer ring GPIO17 (48 LEDs), inner ring GPIO15 (24 LEDs)**, two separate chains,
      Adafruit_NeoPixel NEO_GRB + NEO_KHZ800 (found by disassembling fw 3.6.4: `Adafruit_NeoPixel` ctor
      calls at 0x400f3de2 / 0x400f3df5 with pin 17 / 15, type 0x52; counts from .data). Outer chain: index 0 = first LED clockwise from 12 o'clock, runs clockwise (4 LEDs per hour: hour h = indices 4h..4h+3, 12 = 0). Inner chain: same start and direction, 2 LEDs per hour (hour h = indices 2h..2h+1). Verified, see [ringtest-chain-map.jpg](img/ringtest-chain-map.jpg)
- [ ] Is a level shifter / buffer used for LED data? (any 74xx / SOT-23-5 near the ESP32?)
- [ ] U7 RST pin → which GPIO
- [ ] SW2 function
- [ ] Any light sensor (auto brightness?) / ADC use
- [x] Flash size: 4 MB
- [x] Stock firmware framework: Arduino-ESP32 + WiFiManager (not Tasmota)

## ESP32 GPIOs already used / reserved

| GPIO | Use |
|------|-----|
| 0 | BOOT button (strapping) |
| 1 / 3 | UART0 TX/RX → CH340 |
| 5, 16, 18, 22, 23 | Routed to U7 |
| 6–11 | Internal flash — never touch |

Candidates for the unknowns: 2, 4, 12–15, 17, 19, 21, 25–27, 32–39 (34–39 input only).
