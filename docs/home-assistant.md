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

### The spotprices2ha package itself

This is the package that creates `sensor.shf_data` (plus the SHF rank/price sensors, helpers and the
fetch automation). It is [`spot-price.yaml`](https://github.com/T3m3z/spotprices2ha/blob/main/spot-price.yaml)
v0.2.3 from [T3m3z/spotprices2ha](https://github.com/T3m3z/spotprices2ha), unchanged, MIT licence,
copyright its authors. Data comes from [spot-hinta.fi](https://spot-hinta.fi/) (Finland).

Install: enable packages in `configuration.yaml`
(`homeassistant: packages: !include_dir_named packages`), save this as `packages/spot-price.yaml`,
check the configuration and restart. Then set *SHF Price1/Price2* (your margins in €/kWh) if you want
`TotalPrice` to include them. Check upstream for newer versions.

<details>
<summary>spot-price.yaml (v0.2.3, click to expand)</summary>

```yaml
# Start pack for Spot Price control.
# Version 0.2.3
#
# This is a copy-paste approach in order to get Finnish Electricity Spot Prices to Home Assistant.
# Initial implementation by Temez but feel free to further develop this.
# Special thanks to Mikki who has created the API where data is fetched. For more information see https://spot-hinta.fi/

# This is just a loader for the data which is stored in sensor attributes. Template sensors present actual data.
sensor:
  - platform: rest
    resource: https://api.spot-hinta.fi/TodayAndDayForward?HomeAssistant=true
    name: SHF Electricity price
    unique_id: shf_electricity_price
    scan_interval: 604800 # 7 days, update logic is in the automation
    value_template: 'OK' # static value as sensor is not updated regularly
    json_attributes:
      - "data"
  - platform: statistics
    name: "SHF Average Price 7 Days"
    entity_id: sensor.shf_electricity_price_now
    state_characteristic: mean
    unique_id: shf_average_7days
    precision: 4
    max_age:
      days: 7

template:
  - sensor:
    - name: SHF data # Helper for other sensors to get correct price/rank from array
      unique_id: shf_data
      state: 'DATA'
      availability: '{{ has_value("sensor.shf_electricity_price") }}'
      attributes:
        raw: '{{ state_attr("sensor.shf_electricity_price", "data") if state_attr("sensor.shf_electricity_price", "data") else state_attr("sensor.shf_data", "raw") }}'
        data: >
          {% set output = namespace(value=[]) %}
          {% for value in state_attr("sensor.shf_data","raw") | default([], true) %}
            {% set t = value.DateTime | as_datetime | as_local %}
            {% if t.hour >= state_attr("input_datetime.shf_price2_start", "hour") and t.hour < state_attr("input_datetime.shf_price2_stop", "hour")%}
              {% set x = states("input_number.shf_price2_slider") %}
            {% else %}
              {% set x = states("input_number.shf_price1_slider") %}
            {% endif %}
            {% set price = value.PriceWithTax + x | float %}
            {% set new_data = dict(value, **{"Timestamp": t | as_timestamp, "TotalPrice": price}) %}
            {% set output.value = output.value + [new_data] %}
          {% endfor %}
          {{ output.value }}
  - sensor:
    - name: SHF Rank now
      unique_id: shf_electricity_rank_now
      unit_of_measurement: Rank
      availability: '{{ state_attr("sensor.shf_data", "data") | selectattr("Timestamp", "lt", now() | as_timestamp) | list | length > 0 }}'
      state: >
        {% set today = state_attr('sensor.shf_data', 'data') | selectattr("Timestamp", "lt", today_at("23:59") | as_timestamp) | selectattr("Timestamp", "ge", today_at("0:00") | as_timestamp) %}
        {% set n = state_attr('sensor.shf_data', 'data') | selectattr("Timestamp", "eq", now().replace(minute=0, second=0, microsecond=0) | as_timestamp) | first %}
        {{ (today | sort(attribute="TotalPrice")).index(n) + 1 }}
  - sensor:
    - name: SHF Electricity price now
      unique_id: shf_electricity_price_now
      unit_of_measurement: "€/kWh"
      device_class: monetary
      state: '{{ (state_attr("sensor.shf_data", "data") | selectattr("Timestamp", "lt", now() | as_timestamp) | list)[-1]["TotalPrice"] | round(4) }}'
      availability: '{{ state_attr("sensor.shf_data", "data") | selectattr("Timestamp", "ge", now() | as_timestamp ) | list | length > 0 }}'
      attributes:
        data: '{{ state_attr("sensor.shf_data", "data") }}'
        all_prices: '{{ state_attr("sensor.shf_data", "data") | selectattr("Timestamp", "ge", today_at("0:00") | as_timestamp) | map(attribute="TotalPrice") |list }}'
        today_prices: '{{ state_attr("sensor.shf_data", "data") | selectattr("Timestamp", "ge", today_at("0:00") | as_timestamp) | selectattr("Timestamp", "lt", today_at("23:59") | as_timestamp) | map(attribute="TotalPrice") |list }}'
        tomorrow_prices: '{{ state_attr("sensor.shf_data", "data") | selectattr("Timestamp", "ge", today_at("23:59") | as_timestamp) | map(attribute="TotalPrice") |list }}'
        today_min: '{{ this.attributes.today_prices | default([0]) | min }}'
        today_avg: '{{ this.attributes.today_prices | default([0]) | average | round(4) }}'
        today_max: '{{ this.attributes.today_prices | default([0]) | max }}'
        tomorrow_min: '{{ this.attributes.tomorrow_prices | default([0], true) | min }}'
        tomorrow_avg: '{{ this.attributes.tomorrow_prices | default([0], true) | average | round(4) }}'
        tomorrow_max: '{{ this.attributes.tomorrow_prices | default([0], true) | max }}'
  - sensor:
    - name: SHF Average price today
      unique_id: shf_average_electricity_price
      unit_of_measurement: "€/kWh"
      device_class: monetary
      state: '{{ state_attr("sensor.shf_electricity_price_now", "today_avg") }}'
  - sensor:
    - name: SHF Average price next hours
      unique_id: shf_average_electricity_price_next_hours
      unit_of_measurement: "€/kWh"
      device_class: monetary
      availability: '{{ has_value("sensor.shf_electricity_price_now") }}'
      state: >
            {% set stop = (now() + timedelta(hours=states("input_number.shf_price_avg_slider")|int)) | as_timestamp  %}
            {{ state_attr("sensor.shf_data", "data") | selectattr("Timestamp", "ge", now() | as_timestamp) | selectattr("Timestamp", "lt", stop) | map(attribute="TotalPrice") | average | round(3) }}
  - sensor:
    - name: SHF Control Factor 0-1
      unique_id: shf_control_factor_0-1
      unit_of_measurement: factor
      icon: mdi:gauge
      state: '{{ (states("sensor.shf_control_factor_1") | float(1) /2 + 0.5) }}'
      availability: '{{ state_attr("sensor.shf_control_factor_1", "today_values") is iterable }}'
      attributes:
        today_values: >
            {% set output = namespace(value=[]) %}
            {% for value in state_attr("sensor.shf_control_factor_1", "today_values") %}
                {% set output.value = output.value + [ value | float /2 + 0.5 ] %}
            {% endfor %}
            {{ output.value }}

  - sensor:
    - name: SHF Control Factor +-1
      unique_id: shf_control_factor_+-1
      unit_of_measurement: factor
      icon: mdi:gauge
      state: '{{ state_attr("sensor.shf_control_factor_1", "today_values")[now().hour] | default(1) }}'
      availability: '{{ state_attr("sensor.shf_electricity_price_now", "today_prices") is iterable and state_attr("sensor.shf_electricity_price_now", "today_min") | is_number and state_attr("sensor.shf_electricity_price_now", "today_max") | is_number }}'
      attributes:
        today_values: >
            {% set output = namespace(value=[]) %}
            {% for pnow in state_attr("sensor.shf_electricity_price_now", "today_prices") %}
              {% set pmin = state_attr("sensor.shf_electricity_price_now", "today_min") | float %}
              {% set pmax = state_attr("sensor.shf_electricity_price_now", "today_max") | float %}
              {% set value = max(min((2*(pmax - pnow) / (pmax - pmin)-1) * states('input_number.shf_control_function_factor') | float,1),-1) %}
              {% if states("input_select.shf_control_function" ) == "Linear"  %}
                {% set output.value = output.value + [value | round(2)] %}
              {% else %}
                {% set output.value = output.value + [sin(value*pi/2) | round(2)] %}
              {% endif %}
            {% endfor %}
            {{ output.value }}
  - binary_sensor:
    - name: "SHF Rank acceptable"
      unique_id: shf_rank_acceptable
      device_class: power
      availability: '{{ has_value("sensor.shf_rank_now") }}'
      state: '{{ states("sensor.shf_rank_now")| int <= states("input_number.shf_rank_slider") | int }}'
  - binary_sensor:
    - name: "SHF Price acceptable"
      unique_id: shf_price_acceptable
      device_class: power
      availability: '{{ has_value("sensor.shf_electricity_price_now") }}'
      state: '{{ states("sensor.shf_electricity_price_now") | float <= states("input_number.shf_price_slider") | float }}'
  - binary_sensor:
    - name: "SHF Price or Rank acceptable"
      unique_id: shf_price_or_rank_acceptable
      device_class: power
      state: '{{ is_state("binary_sensor.shf_rank_acceptable", "on") or is_state("binary_sensor.shf_price_acceptable", "on") }}'

input_select:
  shf_control_function:
    name: SHF Control Function
    options:
      - Linear
      - Sinusoidal
    icon: mdi:gauge

input_number:
  shf_control_function_factor:
    name: SHF Control Function Factor
    min: 1
    max: 3
    step: 0.01
    icon: mdi:gauge
    mode: box
  shf_rank_slider:
    name: SHF Max Rank allowed
    min: 1
    max: 24
    step: 1
  shf_price_slider:
    name: SHF Max Price allowed
    min: 0
    max: 4
    step: 0.01
    unit_of_measurement: €/kWh
    icon: mdi:cash-lock
    mode: box
  shf_price_avg_slider:
    name: SHF Average price hours
    min: 1
    max: 24
    step: 1
    unit_of_measurement: h
    icon: mdi:cash-clock
  shf_control_factor:
    name: SHF Control Factor
    min: 0
    max: 1000
    mode: box
  shf_price1_slider:
    name: SHF Price1
    min: 0
    max: 1000
    step: 0.0001
    unit_of_measurement: €
    icon: mdi:cash-lock
    mode: box
  shf_price2_slider:
    name: SHF Price2
    min: 0
    max: 1000
    step: 0.0001
    unit_of_measurement: €
    icon: mdi:cash-lock
    mode: box

input_datetime:
  shf_price2_start:
    name: SHF Price2 start
    has_date: false
    has_time: true
  shf_price2_stop:
    name: SHF Price2 stop
    has_date: false
    has_time: true

# Automation for automatic updating of the sensor. Tries to reduce API load.
automation:
  - id: '1669453089221'
    alias: Intelligent update of electricity price sensor
    description: 'Tries to fetch new data every 15mins after 14 oclock local time. Condition stops unnecessary requests. Random delay disperses API calls.'
    trigger:
    - platform: time_pattern
      minutes: /15
      alias: Trigger every 15mins
    condition:
    - condition: template
      value_template: '{{ state_attr("sensor.shf_data", "data") | selectattr("Timestamp", "ge", (now() + timedelta(hours=9) )| as_timestamp ) | list | length <= 0 }}'
      alias: Check if there isn't data for tomorrow and time is after 14 o'clock
    action:
    - delay: '{{ range(60, 600)|random }}'
      alias: Random delay in order to reduce API congestion
    - service: homeassistant.update_entity
      alias: Trigger the update of the sensor
      target:
        entity_id: sensor.shf_electricity_price
    mode: single
  - id: '1671178061951'
    alias: 'SHF: Validate Price modifiers'
    description: ''
    trigger:
    - platform: state
      entity_id:
      - input_datetime.shf_price2_start
      - input_datetime.shf_price2_stop
    condition: []
    action:
    - service: input_datetime.set_datetime
      data:
        time: '{{ state_attr(''input_datetime.shf_price2_stop'', ''hour'') }}:00'
      target:
        entity_id: input_datetime.shf_price2_stop
    - service: input_datetime.set_datetime
      data:
        time: '{{ state_attr(''input_datetime.shf_price2_start'', ''hour'') }}:00'
      target:
        entity_id: input_datetime.shf_price2_start
    - if:
      - condition: template
        value_template: '{{states(''input_datetime.shf_price2_start'') > states(''input_datetime.shf_price2_stop'')}}'
      then:
      - service: input_datetime.set_datetime
        data:
          time: '{{ states(''input_datetime.shf_price2_stop'') }}'
        target:
          entity_id: input_datetime.shf_price2_start
    mode: single

# Scripts
script:
  cheapest_hours:
    alias: Cheapest hours
    description: "Returns starting time of cheapest period."
    fields:
      start:
        name: "Start time"
        description: "Defines the start time for search. Use template."
        required: true
        example: "{{ now() }}"
        selector:
          datetime:
      stop:
        name: "Stop time"
        description: "Defines the stop time for search. Use template."
        required: true
        example: '{{ now() + timedelta(hours=12) }} or {{ today_at("23:00") }}'
        selector:
          datetime:
      hours:
        name: "Hours"
        description: "Defines the duration of the desired period."
        example: "5"
        required: true
        selector:
          number:
            min: 1
            max: 48
    sequence:
      - variables:
          start_time: >
            {% set output = namespace(value=[], tmp = []) %}
            {% set data = state_attr("sensor.shf_data", "data") | selectattr("Timestamp", "ge", start | as_timestamp) | selectattr("Timestamp", "lt", stop | as_timestamp) | list %}
            {% set hours = hours if hours is number else hours["hours"] %}
            {% set hours = min(hours | int, data | length) %}
            {% for inval in data %}
              {% set output.tmp = data | selectattr("Timestamp", "ge", inval["Timestamp"]) | selectattr("Timestamp", "lt", inval["Timestamp"] + hours*3600) | list%}
              {% if output.tmp | length >= hours %}
                {% set d = dict(inval, **{"PriceTmp": output.tmp | sum(attribute="TotalPrice") }) %}
                {% set output.value = output.value + [d] %}
              {% endif %}
            {%- endfor -%}
            {% set starttime = (output.value | sort(attribute="PriceTmp"))[0]["Timestamp"] %}
            {{ { "start" : starttime } }}
      - stop: Return variable
        response_variable: start_time
        enabled: true
    mode: parallel
    max: 100
```

</details>

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
