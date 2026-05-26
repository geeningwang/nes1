param(
    [string]$Nes1Dir  = "C:\Work\nes1\test\mappy_out\nes1_sl_dump_f1",
    [string]$FceuxDir = "C:\Work\nes1\test\mappy_out\fceux_sl_dump_f1",
    [int]$Frame       = 1,
    [switch]$CompareHighlightedRow
)

Add-Type -AssemblyName System.Drawing

function Get-DumpStatus([string]$dir) {
    $path = Join-Path $dir 'scanline_dump_status.txt'
    $result = @{ Status='unknown'; Reason=''; Source=''; Frame='' }
    if (-not (Test-Path $path)) { return $result }
    foreach ($line in [System.IO.File]::ReadAllLines($path)) {
        $parts = $line -split '=', 2
        if ($parts.Count -ne 2) { continue }
        switch ($parts[0].Trim()) {
            'status' { $result.Status = $parts[1].Trim() }
            'reason' { $result.Reason = $parts[1].Trim() }
            'source' { $result.Source = $parts[1].Trim() }
            'frame'  { $result.Frame  = $parts[1].Trim() }
        }
    }
    return $result
}

function Get-NormalizedText([string]$path) {
    if (-not (Test-Path $path)) { return $null }
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($line in [System.IO.File]::ReadAllLines($path)) {
        $trim = $line.Trim()
        if ($trim -eq '') { continue }
        if ($trim -match '^=+$') { continue }
        $lines.Add($line.TrimEnd())
    }
    return ($lines -join "`n")
}

function Compare-BmpPixels([string]$nes1Path, [string]$fceuxPath, [int]$skipRow) {
    if (-not (Test-Path $nes1Path) -or -not (Test-Path $fceuxPath)) {
        return @{ OK=$false; DiffPx=-1; MaxD=-1; Err='missing file' }
    }

    $b1 = [System.Drawing.Bitmap]::new($nes1Path)
    $b2 = [System.Drawing.Bitmap]::new($fceuxPath)
    try {
        $w = [Math]::Min($b1.Width, $b2.Width)
        $h = [Math]::Min($b1.Height, $b2.Height)
        [int]$diff = 0
        [int]$maxD = 0
        for ([int]$y = 0; $y -lt $h; ++$y) {
            if ($y -eq $skipRow) { continue }
            for ([int]$x = 0; $x -lt $w; ++$x) {
                $c1 = $b1.GetPixel($x, $y)
                $c2 = $b2.GetPixel($x, $y)
                [int]$d = [Math]::Max(
                    [Math]::Abs($c1.R - $c2.R),
                    [Math]::Max([Math]::Abs($c1.G - $c2.G), [Math]::Abs($c1.B - $c2.B)))
                if ($d -gt 0) { ++$diff }
                if ($d -gt $maxD) { $maxD = $d }
            }
        }
        return @{ OK=$true; DiffPx=$diff; MaxD=$maxD; Err='' }
    } finally {
        $b1.Dispose()
        $b2.Dispose()
    }
}

[int]$txtDiff = 0
[int]$bmpDiff = 0
$firstTxt = @()
$firstBmp = @()
$nes1Status = Get-DumpStatus $Nes1Dir
$fceuxStatus = Get-DumpStatus $FceuxDir

if ($nes1Status.Status -ne 'valid' -or $fceuxStatus.Status -ne 'valid') {
    Write-Host "Scanline dump compare, frame $Frame"
    Write-Host "  Status: unavailable"
    Write-Host "  NES1:  $($nes1Status.Status)  $($nes1Status.Reason)"
    Write-Host "  FCEUX: $($fceuxStatus.Status)  $($fceuxStatus.Reason)"
    exit 0
}

for ([int]$sl = 0; $sl -lt 240; ++$sl) {
    $nTxt = Join-Path $Nes1Dir  ("nes1_frame_{0:D4}_sl_{1:D3}.txt" -f $Frame, $sl)
    $fTxt = Join-Path $FceuxDir ("fceux_frame_{0:D4}_sl_{1:D3}.txt" -f $Frame, $sl)
    $nNorm = Get-NormalizedText $nTxt
    $fNorm = Get-NormalizedText $fTxt
    if ($nNorm -ne $fNorm) {
        ++$txtDiff
        if ($firstTxt.Count -lt 10) { $firstTxt += $sl }
    }

    $nBmp = Join-Path $Nes1Dir  ("nes1_frame_{0:D4}_sl_{1:D3}.bmp" -f $Frame, $sl)
    $fBmp = Join-Path $FceuxDir ("fceux_frame_{0:D4}_sl_{1:D3}.bmp" -f $Frame, $sl)
    [int]$skip = if ($CompareHighlightedRow) { -1 } else { $sl }
    $cmp = Compare-BmpPixels $nBmp $fBmp $skip
    if ((-not $cmp.OK) -or $cmp.DiffPx -ne 0) {
        ++$bmpDiff
        if ($firstBmp.Count -lt 10) {
            $firstBmp += ("SL{0}: {1}px maxD={2}" -f $sl, $cmp.DiffPx, $cmp.MaxD)
        }
    }
}

Write-Host "Scanline dump compare, frame $Frame"
Write-Host "  Text diffs: $txtDiff / 240"
if ($firstTxt.Count -gt 0) { Write-Host "    first: $($firstTxt -join ', ')" }
Write-Host "  BMP diffs:  $bmpDiff / 240"
if (-not $CompareHighlightedRow) { Write-Host "    highlighted row ignored; pass -CompareHighlightedRow to include it" }
if ($firstBmp.Count -gt 0) { Write-Host "    first: $($firstBmp -join '; ')" }
