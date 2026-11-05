param([string]$Flutter = 'flutter')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location (Join-Path $root 'mobile/SmartSchoolbagApp')
try {
    & $Flutter pub get
    if ($LASTEXITCODE -ne 0) { throw 'flutter pub get failed' }
    & $Flutter analyze
    if ($LASTEXITCODE -ne 0) { throw 'flutter analyze reported issues; resolve before release' }
    & $Flutter test
    if ($LASTEXITCODE -ne 0) { throw 'flutter test failed' }
    & $Flutter build apk --release
    if ($LASTEXITCODE -ne 0) { throw 'flutter build failed' }
} finally { Pop-Location }
