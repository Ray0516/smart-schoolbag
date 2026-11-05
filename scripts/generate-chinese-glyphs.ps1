$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$glyphFile = Join-Path $repoRoot 'firmware\EPaperTest\ChineseGlyphs.h'
$encoding = [System.Text.Encoding]::GetEncoding(950)
Add-Type -AssemblyName System.Drawing

# Keep hand-tuned bitmaps, then fill the Traditional Chinese Big5 character set
# from Microsoft's locally installed JhengHei UI font for phone-defined names.
$existing = @{}
foreach ($line in Get-Content -LiteralPath $glyphFile) {
  $match = [regex]::Match($line, '^\s*\{0x([0-9A-Fa-f]+),\s*\{([^}]*)\}\},\s*//\s*(.)\s*$')
  if (-not $match.Success) { continue }
  $codepoint = [Convert]::ToInt32($match.Groups[1].Value, 16)
  $existing[$codepoint] = $line.Trim()
}

$fontPath = Join-Path $env:WINDIR 'Fonts\msjh.ttc'
if (-not (Test-Path -LiteralPath $fontPath)) {
  throw "Microsoft JhengHei font not found: $fontPath"
}
$font = [System.Drawing.Font]::new('Microsoft JhengHei UI', 12, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$bitmap = [System.Drawing.Bitmap]::new(12, 12, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
$graphics.PageUnit = [System.Drawing.GraphicsUnit]::Pixel
$format = [System.Drawing.StringFormat]::GenericTypographic
$blackBrush = [System.Drawing.Brushes]::Black
$white = [System.Drawing.Color]::White

function Get-GlyphRows([string]$Character) {
  $graphics.Clear($white)
  $graphics.DrawString($Character, $font, $blackBrush, [single]0, [single]-2, $format)
  $rows = [System.Collections.Generic.List[int]]::new()
  for ($y = 0; $y -lt 12; $y++) {
    $bits = 0
    for ($x = 0; $x -lt 12; $x++) {
      if ($bitmap.GetPixel($x, $y).R -lt 128) { $bits = $bits -bor (0x0800 -shr $x) }
    }
    $rows.Add($bits)
  }
  return ,$rows.ToArray()
}

try {
  $replacementRows = (Get-GlyphRows ([string][char]0xFFFD)) -join ','
  $codepoints = [System.Collections.Generic.SortedSet[int]]::new()
  foreach ($cp in $existing.Keys) { [void]$codepoints.Add([int]$cp) }

  for ($lead = 0x81; $lead -le 0xFE; $lead++) {
    foreach ($trail in (@(0x40..0x7E) + @(0xA1..0xFE))) {
      $bytes = [byte[]]@($lead, $trail)
      $text = $encoding.GetString($bytes)
      if ($text.Length -ne 1) { continue }
      $cp = [int][char]$text[0]
      if (($cp -ge 0x4E00 -and $cp -le 0x9FFF) -or
          ($cp -ge 0x3400 -and $cp -le 0x4DBF) -or
          ($cp -ge 0xF900 -and $cp -le 0xFAFF)) {
        [void]$codepoints.Add($cp)
      }
    }
  }

  $lines = [System.Collections.Generic.List[string]]::new()
  $lines.Add('#pragma once')
  $lines.Add('#include <Arduino.h>')
  $lines.Add('// 12x12 hand-tuned plus Traditional Chinese Big5 glyphs from Microsoft JhengHei.')
  $lines.Add('struct ChineseGlyph12 { uint16_t codepoint; uint16_t rows[12]; };')
  $lines.Add('const ChineseGlyph12 ZH_GLYPHS[] PROGMEM = {')

  foreach ($cp in $codepoints) {
    if ($existing.ContainsKey($cp)) {
      $lines.Add('  ' + $existing[$cp])
      continue
    }
    $character = [string][char]$cp
    $rows = Get-GlyphRows $character
    if (($rows -join ',') -eq $replacementRows) { continue }
    $hexRows = ($rows | ForEach-Object { '0x{0:X3}' -f $_ }) -join ', '
    $codepointHex = '{0:X4}' -f $cp
    $lines.Add('  {0x' + $codepointHex + ', {' + $hexRows + '}}, // ' + $character)
  }

  $lines.Add('};')
  $lines.Add('constexpr uint16_t ZH_GLYPH_COUNT = sizeof(ZH_GLYPHS) / sizeof(ZH_GLYPHS[0]);')
  Set-Content -LiteralPath $glyphFile -Value $lines -Encoding utf8
  Write-Host "Generated $($lines.Count - 6) Traditional Chinese glyphs in $glyphFile"
}
finally {
  $format.Dispose()
  $graphics.Dispose()
  $bitmap.Dispose()
  $font.Dispose()
}
