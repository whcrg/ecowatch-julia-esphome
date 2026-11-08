# Runs ESPHome with the settings this PC needs. Usage: ./tools/esphome.ps1 run esphome/ledfinder.yaml --device COM9
# - Uses `esphome` from PATH, or $env:ESPHOME_EXE (e.g. the esphome.exe in a venv's Scripts folder).
# - Keeps the ESP-IDF toolchain on a short path: Windows long-path support is off here and the
#   default cache path is too long for the toolchain.
# - Must be run from PowerShell/cmd: ESP-IDF refuses to install from Git Bash (MSYS).
$ErrorActionPreference = "Stop"
if (-not $env:ESPHOME_ESP_IDF_PREFIX) { $env:ESPHOME_ESP_IDF_PREFIX = "C:\ESPHome\idf" }
$exe = $env:ESPHOME_EXE
if (-not $exe) { $exe = "esphome" }
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
& $exe @args
exit $LASTEXITCODE
