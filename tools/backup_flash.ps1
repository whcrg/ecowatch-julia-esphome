# Backs up the complete flash of the EcoWatch Julia (ESP32-WROOM-32) to dumps/.
# Usage: ./tools/backup_flash.ps1 -Port COM9 [-Size 4MB] [-Python <python with esptool>]
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [string]$Size = "ALL",
    [int]$Baud = 460800,
    [string]$Python = "python"   # a python that has esptool installed
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dumps = Join-Path $root "dumps"
New-Item -ItemType Directory -Force $dumps | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$info = Join-Path $dumps "chip-info-$stamp.txt"
$bin = Join-Path $dumps "ecowatch-julia-original-$stamp.bin"

Write-Host "Reading chip info..."
& $Python -m esptool --port $Port chip_id | Tee-Object -FilePath $info
& $Python -m esptool --port $Port flash_id | Tee-Object -FilePath $info -Append
& $Python -m esptool --port $Port read_mac | Tee-Object -FilePath $info -Append

Write-Host "Reading flash ($Size) to $bin ..."
& $Python -m esptool --port $Port --baud $Baud read_flash 0x0 $Size $bin
if ($LASTEXITCODE -ne 0) { throw "read_flash failed" }

# Read a second time and compare, so a flaky USB link doesn't give a corrupt backup.
$verify = "$bin.verify"
& $Python -m esptool --port $Port --baud $Baud read_flash 0x0 $Size $verify
$h1 = (Get-FileHash $bin -Algorithm SHA256).Hash
$h2 = (Get-FileHash $verify -Algorithm SHA256).Hash
Remove-Item $verify
if ($h1 -ne $h2) { throw "Two reads differ ($h1 vs $h2) - retry with a lower -Baud" }

"$h1  $(Split-Path -Leaf $bin)" | Out-File -Encoding ascii "$bin.sha256"
Write-Host "OK: $bin"
Write-Host "SHA256: $h1"
Write-Host "Copy this file somewhere safe outside the project too."
