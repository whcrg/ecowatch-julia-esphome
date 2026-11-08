# Reverse-engineering workflow

Goal: learn everything the stock firmware knows about the hardware, then replace it with ESPHome —
without ever losing the ability to go back.

## 0. Tools

```bash
pip install esptool esphome
```

The board has a CH340 with auto-reset (Q3/Q4), so just plug in micro-USB. Install the CH340
driver if Windows doesn't show a COM port.

## 1. Look at the stock firmware while it is still running

### 1a. Web UI (no cables needed)

With the clock set up on your Wi-Fi:

```bash
python tools/web_recon.py ecowatch.local      # or the clock's IP
```

Saves all pages to `dumps/web/` and checks for Tasmota (`/cm?cmnd=...`) and WiFiManager
endpoints.

**If it turns out to be Tasmota-derived:** you get the GPIO map for free. Look at
`cm_cmnd_Template.txt` / `cm_cmnd_Gpio.txt` / `cm_cmnd_Status_200.txt`. Also check the
web UI's *Consoles → Manage File System* for `autoexec.be`, `autoexec.bat` or `display.ini`, and
download them.

On the **Update** page, click **"Get latest firmware"**. It downloads the vendor's `.bin`.
Save it to `dumps/` and analyse it (step 3). This also tells you the download URL.

### 1b. MQTT (optional)

Enable MQTT in the API page and point it at your broker. Then subscribe to `ecowatch/#` (the firmware uses `ecowatch/...`, not the manual's `energywatch/...`) to see
the payloads. Useful if you want the ESPHome version to keep the same topics.

### 1c. Serial boot log

Open the serial port at 115200 baud, press RST and save the output to `dumps/boot-log.txt`.

```bash
python -m serial.tools.miniterm COM9 115200
```

Note: pressing RST counts toward the 5-press factory reset. Wait a few seconds after each press.

## 2. Back up the original flash (DO THIS BEFORE FLASHING ANYTHING)

```powershell
./tools/backup_flash.ps1 -Port COM9
```

This reads chip info + the full flash to `dumps/` twice and checks that both reads match.
Keep a copy somewhere outside this folder too. `dumps/` is git-ignored because the image
contains the vendor's code and your Wi-Fi credentials.

Restore any time with:

```bash
esptool --port COM9 write_flash 0x0 dumps/ecowatch-julia-original-<date>.bin
```

## 3. Analyse the dump

```bash
python tools/analyze_dump.py dumps/ecowatch-julia-original-<date>.bin
python tools/analyze_dump.py dumps/<vendor-update>.bin      # a bare app image works too
```

Prints the partition table, and the app description of each app (project name, version, build
date, IDF version). It writes all strings to `*.strings.txt` and the interesting ones to
`*.interesting.txt`, and reports whether it looks like Tasmota, WiFiManager, FastLED or
NeoPixelBus.

Pin numbers are rarely strings. For the LED data pin, look in Ghidra
(<https://github.com/Ebiroll/ghidra-xtensa>) for constants passed to `rmt_config` /
`FastLED.addLeds` / the `Adafruit_NeoPixel` constructor. In practice a continuity test (step 4)
is faster.

NVS (settings, Wi-Fi creds) can be decoded with ESP-IDF's `nvs_tool.py`.

## 4. Probe the hardware

- **Continuity:** from the first LED's DIN pad (follow the trace into the ring), from the U7
  RST pin and from SW2 to the ESP32-WROOM pads. The module's pin map is in the ESP32-WROOM-32
  datasheet. A 33–470 Ω series resistor on the data line is normal.
- **[esphome/ledfinder.yaml](../esphome/ledfinder.yaml):** drives 4 candidate pins as LED strips
  at once. Swap the pin set on the command line until the ring lights up.
- **[esphome/probe.yaml](../esphome/probe.yaml):** shows every free GPIO and ADC pin live in a
  web UI, for buttons, SW2 and sensors.

Record results in [hardware.md](hardware.md).

## 5. Build the real firmware

Fill in the TODOs in [`esphome/ecowatch-julia.yaml`](../esphome/ecowatch-julia.yaml), flash
over USB once, then OTA afterwards.

Uploading an ESPHome `.bin` through the stock Update page *might* work, but the stock
partition layout is unknown until step 3. Use USB for the first flash.
