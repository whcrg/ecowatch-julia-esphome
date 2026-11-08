# Home Assistant setup

After flashing [`esphome/ecowatch-julia.yaml`](../esphome/ecowatch-julia.yaml) and adopting the
device in HA, you get:

| Entity | What |
|--------|------|
| Outer ring mode (select) | `Price` (stock behaviour), `Custom` or `Manual` |
| Inner ring mode (select) | `Tariff`, `Power`, `Price`, `Custom` or `Manual` |
| Outer ring / Inner ring (lights) | Brightness in every mode; colour and effects in `Manual` |
| Price colour mode (select) | `Daily average`, `Own limits`, `Green hours` |
| Low / High price limit, Green hours, Average offset (numbers) | Settings for the colour modes |
| Negative price always green, Hide hours without price (switches) | Same as the stock options |
| Flash current time (select: Off / Current hour / Current quarter), Flash period (ms, default 500) | Price mode: flash the current hour's LEDs or just the current slot |
| LED current limit (mA, default 600) / LED current (estimated) | Caps both rings together in every mode; see below |
| Tariff start/stop hour, Tariff weekdays only | Inner ring `Tariff` mode (orange LEDs) |
| Power ring full scale | Inner ring `Power` mode; reads `sensor.power_consumption` (change `power_entity` in the YAML) |
| Current price, Current price zone (1 green / 2 yellow / 3 red / 0 none) | For your own automations, e.g. replacing the stock Shelly control |
| Price slots stored | 0 after a reboot until HA sends prices again |

## Display rules

- Both rings are 12-hour dials. Each LED shows the **next** occurrence of its position: the current
  slot plus the following 12 hours. Outer = 15 min per LED, inner = 30 min per LED.
- Hourly prices fill 4 outer LEDs each; 15-minute prices fill one LED each.
- **Daily average:** price ≤ (day average + offset) is green. The range from there to the day's
  maximum is split in two: lower half yellow, upper half red.
- **Own limits:** ≤ low limit green, ≤ high limit yellow, otherwise red.
- **Green hours:** the N cheapest hours of each day are green; the rest follow the daily average rule.
- No price for a slot → purple (or off with "Hide hours without price").
- No valid time yet → one blue LED walking round.

Prices are in whatever unit HA sends. The limits use that same unit. If you want taxes and
transfer fees included, add them in the automation before sending.

## Sending prices (spotprices2ha / spot-hinta.fi) — tested

[spotprices2ha](https://github.com/T3m3z/spotprices2ha) keeps 15-minute prices for today and
tomorrow in `sensor.shf_data` → attribute `data`: a list of
`{Timestamp, PriceNoTax, PriceWithTax, TotalPrice, Rank, DateTime}`, prices in €/kWh.
`PriceNoTax × 100` = raw spot in c/kWh, the same basis as the stock clock. Use `PriceWithTax`
or `TotalPrice` (adds your SHF Price1/Price2 margins) if you want the colours to follow the real cost.

Live automation in HA: **EcoWatch Julia send spot prices** (`automation.ecowatch_julia_send_spot_prices`):

```yaml
alias: EcoWatch Julia send spot prices
mode: queued
max: 3
triggers:
  - trigger: state            # new prices fetched
    entity_id: sensor.shf_data
    attribute: data
  - trigger: numeric_state    # clock rebooted / reconnected with an empty store
    entity_id: sensor.ecowatch_julia_price_slots_stored
    below: 1
  - trigger: homeassistant
    event: start
  - trigger: time_pattern     # hourly backstop
    minutes: 3
conditions:
  - condition: state
    entity_id: sensor.shf_data
    state: DATA
actions:
  - variables:
      slots: "{{ state_attr('sensor.shf_data', 'data') | default([], true) }}"
  - condition: template
    value_template: "{{ slots | length > 1 }}"
  - action: esphome.ecowatch_julia_set_prices
    data:
      start: "{{ slots[0].Timestamp | int }}"
      resolution: "{{ ((slots[1].Timestamp - slots[0].Timestamp) / 60) | int }}"
      prices: "{{ slots | map(attribute='PriceNoTax') | map('multiply', 100) | list }}"
```

Verified 2026-11-08: clock reported 192 slots stored and the same current price as the sensor.

## Sending prices (Nord Pool integration)

The clock keeps prices in RAM only, so HA re-sends them every hour and when the clock reconnects.
Replace `YOUR_NORDPOOL_ENTRY_ID` and `FI` (your area). Nord Pool returns €/MWh; `× 0.1` turns
that into c/kWh.

```yaml
alias: EcoWatch - send electricity prices
mode: queued
triggers:
  - trigger: time_pattern
    minutes: 5
  - trigger: homeassistant
    event: start
  - trigger: state
    entity_id: sensor.ecowatch_julia_price_slots_stored
    to: "0"
actions:
  - action: nordpool.get_prices_for_date
    data:
      config_entry: YOUR_NORDPOOL_ENTRY_ID
      date: "{{ now().date() }}"
      areas: FI
    response_variable: today
  - action: nordpool.get_prices_for_date
    continue_on_error: true   # tomorrow's prices only exist after ~13:00 CET
    data:
      config_entry: YOUR_NORDPOOL_ENTRY_ID
      date: "{{ (now() + timedelta(days=1)).date() }}"
      areas: FI
    response_variable: tomorrow
  - variables:
      slots: >-
        {{ today['FI'] + (tomorrow['FI'] if tomorrow is defined and tomorrow and 'FI' in tomorrow else []) }}
  - action: esphome.ecowatch_julia_set_prices
    data:
      start: "{{ as_timestamp(slots[0].start) | int }}"
      resolution: "{{ ((as_timestamp(slots[1].start) - as_timestamp(slots[0].start)) / 60) | int }}"
      prices: "{{ slots | map(attribute='price') | map('multiply', 0.1) | list }}"
```

Any other price source works the same way: call `esphome.ecowatch_julia_set_prices` with the
start time of the first price (unix seconds), the slot length in minutes, and the list of prices.

## Custom mode: paint individual LEDs

LED numbering: 0 = first LED clockwise from 12 o'clock, running clockwise. Outer ring 0–47
(4 per hour, hour *h* = 4h…4h+3), inner ring 0–23 (2 per hour). Colours are `0xRRGGBB` integers
(`16711680` = red). Values are **linear** (gamma off in this mode): `0x800000` draws half the current
of `0xFF0000`. The ring's light brightness still scales everything. Calling an action switches that
ring to `Custom` automatically; pick another mode to leave it. The buffer is kept in RAM until reboot.

| Action | Fields |
|--------|--------|
| `esphome.ecowatch_julia_set_pixels` | `ring` (`outer`/`inner`), `offset` (first LED), `colors` (list) |
| `esphome.ecowatch_julia_set_pixels_brightness` | as above + `brightness` (list of 0–100 %, per LED) |
| `esphome.ecowatch_julia_clear_pixels` | `ring` |

```yaml
# Rainbow on the inner ring, fading in clockwise
action: esphome.ecowatch_julia_set_pixels_brightness
data:
  ring: inner
  offset: 0
  colors: "{% set ns = namespace(c=[]) %}{% for i in range(24) %}{% set ns.c = ns.c + [[0xFF0000,0xFF8000,0xFFFF00,0x00FF00,0x00FFFF,0x0000FF,0xFF00FF][i % 7]] %}{% endfor %}{{ ns.c }}"
  brightness: "{{ range(24) | map('multiply', 4) | list }}"
---
# Light the 4 LEDs of 3 o'clock blue, leave the rest as they are
action: esphome.ecowatch_julia_set_pixels
data: {ring: outer, offset: 12, colors: [255, 255, 255, 255]}
```

## LED current limiter

Both rings together are estimated at about 20 mA per colour channel at full duty (72 LEDs × 60 mA
≈ 4.3 A at full white). Every 200 ms the firmware estimates the draw and, above **LED current
limit**, scales both rings down by the same factor. This applies to every mode, including Manual
effects. The clock ships with only a USB-A to barrel-jack cord, no charger, so the supply is whatever
USB port or plug you use. 600 mA suits a ~1 A USB plug; with a known 5 V / 2 A plug, 1500 mA is reasonable.

## More automation ideas

- **Dynamic limits:** hourly, set *Low/High price limit* to e.g. 75 % / 115 % of a monthly average
  price sensor and select colour mode *Own limits*. Use the same unit and tax basis as the prices you send.
- **Status ring:** paint the whole inner ring one colour from any HA state (a reminder, an alarm, a
  chore) with `esphome.ecowatch_julia_set_pixels`, e.g. `colors: "{{ [16711680]*24 }}"` for red or
  `"{{ [0]*24 }}"` for off. Also trigger on `sensor.ecowatch_julia_price_slots_stored` below 1 so the
  colour is resent after the clock reboots (the pixel buffer is RAM only).
- After moving from the stock firmware, remove its old MQTT device (`EcoWatch-Julia-<id>`) from HA
  and any `rest_command` entries that called the stock web API.
