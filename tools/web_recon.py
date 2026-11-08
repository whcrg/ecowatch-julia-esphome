"""Save the stock firmware's web UI and probe for Tasmota / WiFiManager endpoints.

Usage: python tools/web_recon.py [host]      (default host: ecowatch.local)

Only sends GET requests to paths that are read-only on Tasmota / WiFiManager / Arduino
WebServer. Everything is saved to dumps/web/<host>/ so it can be diffed and grepped later.
"""
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

PATHS = [
    "/",
    "/discovery",
    "/info",
    "/update",
    "/wifi",
    "/param",
    "/json",
    "/api",
    "/status",
    "/version",
    # Tasmota: these answer with JSON if the firmware is Tasmota-derived.
    "/cm?cmnd=Status%200",
    "/cm?cmnd=Template",
    "/cm?cmnd=Module",
    "/cm?cmnd=Gpio",
    "/cm?cmnd=Gpios",
    "/cm?cmnd=UfsType",
    "/cm?cmnd=Ufs",
    "/ufsd",
]


def safe_name(path: str) -> str:
    name = re.sub(r"[^A-Za-z0-9._-]+", "_", path.strip("/")) or "index"
    return name + ".txt"


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "ecowatch.local"
    out = Path(__file__).resolve().parent.parent / "dumps" / "web" / host
    out.mkdir(parents=True, exist_ok=True)

    for path in PATHS:
        url = f"http://{host}{path}"
        try:
            with urllib.request.urlopen(url, timeout=8) as r:
                body = r.read()
                status = r.status
                headers = str(r.headers)
        except urllib.error.HTTPError as e:
            body, status, headers = e.read(), e.code, str(e.headers)
        except Exception as e:  # noqa: BLE001 - recon tool, keep going
            print(f"{path:<28} ERROR {e}")
            continue
        (out / safe_name(path)).write_bytes(
            f"GET {url}\nHTTP {status}\n{headers}\n".encode() + body)
        hint = ""
        if b"Tasmota" in body or b'"StatusFWR"' in body:
            hint = "  <-- Tasmota!"
        elif b"WiFiManager" in body or b"wifisave" in body:
            hint = "  <-- WiFiManager"
        print(f"{path:<28} HTTP {status}  {len(body):>6} bytes{hint}")

    print(f"\nSaved to {out}")


if __name__ == "__main__":
    main()
