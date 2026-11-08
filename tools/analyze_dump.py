"""Quick first-pass analysis of an ESP32 full-flash dump.

Usage: python tools/analyze_dump.py dumps/ecowatch-julia-original-XXXX.bin

Prints the partition table and the esp_app_desc_t of each app partition, and writes
printable strings (plus a short list of "interesting" hits) next to the dump.
"""
import re
import struct
import sys
from pathlib import Path

PART_TABLE_OFFSET = 0x8000
PART_MAGIC = b"\xaa\x50"
APP_DESC_MAGIC = 0xABCD5432

TYPES = {0x00: "app", 0x01: "data"}
APP_SUBTYPES = {0x00: "factory", 0x10: "ota_0", 0x11: "ota_1", 0x12: "ota_2", 0x20: "test"}
DATA_SUBTYPES = {0x00: "ota", 0x01: "phy", 0x02: "nvs", 0x03: "coredump", 0x04: "nvs_keys",
                 0x05: "efuse", 0x80: "esphttpd", 0x81: "fat", 0x82: "spiffs", 0x83: "littlefs"}

INTERESTING = re.compile(
    rb"st77|ili9|gc9a|ssd1|tft|lvgl|ws281|sk68|neopixel|fastled|rmt|ledc|"
    rb"https?://|mqtt|wss?://|api|elpris|nordpool|spot|price|se[1-4]\b|"
    rb"waltrix|ecowatch|energywatch|julia|shelly|homewizard|elpris|"
    rb"tasmota|wifimanager|berry|gpio|pin|ntp|ota|version",
    re.IGNORECASE,
)

# Substring -> what it suggests about the stock firmware.
FINGERPRINTS = {
    b"Tasmota": "Tasmota (or derived)",
    b"tasmota": "Tasmota (or derived)",
    b"WiFiManager": "Arduino + tzapu WiFiManager",
    b"*wm:": "Arduino + tzapu WiFiManager",
    b"FastLED": "FastLED",
    b"NeoPixelBus": "NeoPixelBus",
    b"Adafruit_NeoPixel": "Adafruit NeoPixel",
    b"led_strip": "ESP-IDF led_strip",
    b"PubSubClient": "PubSubClient (MQTT)",
    b"arduino-esp32": "Arduino core",
    b"ESPAsyncWebServer": "ESPAsyncWebServer",
    b"esphome": "ESPHome",
}


def cstr(b: bytes) -> str:
    return b.split(b"\0", 1)[0].decode("ascii", "replace")


def parse_partitions(data: bytes):
    parts = []
    for off in range(PART_TABLE_OFFSET, PART_TABLE_OFFSET + 0xC00, 32):
        entry = data[off:off + 32]
        if entry[:2] != PART_MAGIC:
            break
        ptype, subtype, offset, size = struct.unpack_from("<BBII", entry, 2)
        label = cstr(entry[12:28])
        flags = struct.unpack_from("<I", entry, 28)[0]
        sub = (APP_SUBTYPES if ptype == 0 else DATA_SUBTYPES).get(subtype, hex(subtype))
        parts.append(dict(label=label, type=TYPES.get(ptype, hex(ptype)), subtype=sub,
                          offset=offset, size=size, flags=flags))
    return parts


def parse_app_desc(data: bytes, offset: int):
    if data[offset] != 0xE9:
        return None
    # 24-byte image header + 8-byte first segment header, then esp_app_desc_t.
    d = offset + 32
    magic, secure_ver = struct.unpack_from("<II", data, d)
    if magic != APP_DESC_MAGIC:
        return {"note": "image present but no esp_app_desc_t (very old IDF?)"}
    return {
        "version": cstr(data[d + 16:d + 48]),
        "project": cstr(data[d + 48:d + 80]),
        "time": cstr(data[d + 80:d + 96]),
        "date": cstr(data[d + 96:d + 112]),
        "idf_ver": cstr(data[d + 112:d + 144]),
        "secure_version": secure_ver,
    }


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = Path(sys.argv[1])
    data = path.read_bytes()
    print(f"{path.name}: {len(data)} bytes ({len(data) // 1024 // 1024} MB)\n")

    parts = parse_partitions(data) if len(data) > PART_TABLE_OFFSET else []
    if parts:
        print(f"{'label':<16}{'type':<6}{'subtype':<10}{'offset':>10}{'size':>10}  flags")
        for p in parts:
            print(f"{p['label']:<16}{p['type']:<6}{p['subtype']:<10}"
                  f"{p['offset']:>#10x}{p['size']:>#10x}  {p['flags']:#x}")
        for p in parts:
            if p["type"] == "app" and p["offset"] < len(data):
                desc = parse_app_desc(data, p["offset"])
                print(f"\n[{p['label']}] " + (str(desc) if desc else "empty"))
    elif data[:1] == b"\xe9":
        print("Bare app image (e.g. an OTA .bin):")
        print(parse_app_desc(data, 0))
    else:
        print("No partition table at 0x8000 and not an app image — encrypted flash?")

    found = sorted({v for k, v in FINGERPRINTS.items() if k in data})
    print("\nFingerprints: " + (", ".join(found) if found else "none"))

    strings = [m.group() for m in re.finditer(rb"[\x20-\x7e]{6,}", data)]
    out = path.with_suffix(".strings.txt")
    out.write_bytes(b"\n".join(strings))
    hits = sorted({s for s in strings if INTERESTING.search(s)})
    hits_out = path.with_suffix(".interesting.txt")
    hits_out.write_bytes(b"\n".join(hits))
    print(f"\n{len(strings)} strings -> {out.name}")
    print(f"{len(hits)} interesting strings -> {hits_out.name}")


if __name__ == "__main__":
    main()
