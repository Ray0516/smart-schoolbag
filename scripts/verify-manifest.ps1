$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content -Raw -LiteralPath (Join-Path $root 'SOURCE-MANIFEST.json') | ConvertFrom-Json
foreach ($entry in $manifest.files) {
    $file = Join-Path $root $entry.path
    if (-not (Test-Path -LiteralPath $file)) { throw "Missing: $($entry.path)" }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) { throw "Modified since handoff: $($entry.path)" }
}
Write-Host "Verified $($manifest.files.Count) baseline source files. Update manifest deliberately after changes."
