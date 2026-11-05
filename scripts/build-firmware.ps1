param([string]$ArduinoCli = 'arduino-cli')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'firmware/EPaperTest'
$out = Join-Path $root 'artifacts/firmware'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$fqbn = 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=4M,PSRAM=disabled,PartitionScheme=huge_app'
& $ArduinoCli compile --fqbn $fqbn --output-dir $out $source
if ($LASTEXITCODE -ne 0) { throw "Firmware build failed: $LASTEXITCODE" }
Write-Host "Build complete: $out (no upload)"
