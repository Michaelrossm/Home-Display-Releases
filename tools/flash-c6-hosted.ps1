[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM[0-9]+$')]
    [string]$Port,

    [string]$Image = (Join-Path $env:TEMP 'home-display-c6\esp32c6-v2.12.13.bin'),

    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
$expectedSha256 = 'F8509949826C197A4FA3C7958943B4E8985CFFDFDF4204C2847D8953905121C9'
$imageUrl = 'https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.12.13.bin'
$projectRoot = Split-Path -Parent $PSScriptRoot
$pioPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'

if ($Port -eq 'COM7') {
    throw 'COM7 ist der P4-USB-Port. Fuer den C6 ausschliesslich den separaten TTL-UART-Port angeben.'
}
if (-not (Test-Path -LiteralPath $pioPython)) {
    throw 'PlatformIO Python wurde nicht gefunden. PlatformIO zuerst installieren.'
}

if (-not (Test-Path -LiteralPath $Image)) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Image) | Out-Null
    Invoke-WebRequest -Uri $imageUrl -OutFile $Image
}

$actualSha256 = (Get-FileHash -LiteralPath $Image -Algorithm SHA256).Hash
if ($actualSha256 -ne $expectedSha256) {
    throw "C6-Image-Pruefsumme stimmt nicht: $actualSha256"
}

Write-Host 'C6-Image geprueft: ESP-Hosted 2.12.13'
Write-Host 'Vor dem Fortsetzen: C6-Programmierport IO9 mit GND verbinden, TTL anschliessen und beim Einschalten P4-BOOT gedrueckt halten.'
if ($VerifyOnly) { return }
if ($PSCmdlet.ShouldProcess("ESP32-C6 an $Port", 'ESP-Hosted-Firmware schreiben')) {
    & $pioPython -m esptool --chip esp32c6 --port $Port --baud 460800 write-flash 0x0 $Image
    if ($LASTEXITCODE -ne 0) { throw "esptool fehlgeschlagen ($LASTEXITCODE)" }
    Write-Host 'C6-Firmware geschrieben. IO9-GND entfernen und Board vollstaendig neu starten.'
}
