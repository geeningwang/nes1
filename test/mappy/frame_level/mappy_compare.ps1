param(
    [string]$Nes1Dir  = "C:\Work\nes1\test\mappy_out\nes1_out",
    [string]$FceuxDir = "C:\Work\nes1\test\mappy_out\fceux_out",
    [string]$OutHtml  = "C:\Work\nes1\test\mappy_out\report.html",
    [int]$NFrames     = 300,
    [int]$Interval    = 30
)
Add-Type -AssemblyName System.Drawing

# ── Pixel-by-pixel image comparison ──────────────────────────────────────────
# Handles NES overscan: nes1=256x240 (full PPU), FCEUX=256x224 (visible rows).
# Compares the shared 224 rows with an 8-row top offset on the larger image.
# Returns: @{ MaxDelta; MismatchPixels; Misaligned; SizeMsg; OK }
$TOL = 10

function Compare-Images([string]$path1, [string]$path2) {
    try {
        $bmp1 = [System.Drawing.Bitmap]::new($path1)
        $bmp2 = [System.Drawing.Bitmap]::new($path2)
        if ($bmp1.Width -ne $bmp2.Width) {
            $msg = '{0}x{1} vs {2}x{3}' -f $bmp1.Width,$bmp1.Height,$bmp2.Width,$bmp2.Height
            $bmp1.Dispose(); $bmp2.Dispose()
            return @{ MaxDelta=999; MismatchPixels=-1; Misaligned=$true; SizeMsg=$msg; OK=$true }
        }
        $w     = $bmp1.Width
        [int]$h    = [Math]::Min($bmp1.Height, $bmp2.Height)
        [int]$off1 = if ($bmp1.Height -gt $bmp2.Height) { ($bmp1.Height - $bmp2.Height) / 2 } else { 0 }
        [int]$off2 = if ($bmp2.Height -gt $bmp1.Height) { ($bmp2.Height - $bmp1.Height) / 2 } else { 0 }
        $rect1 = [System.Drawing.Rectangle]::new(0, $off1, $w, $h)
        $rect2 = [System.Drawing.Rectangle]::new(0, $off2, $w, $h)
        $fmt   = [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
        $bd1   = $bmp1.LockBits($rect1, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $fmt)
        $bd2   = $bmp2.LockBits($rect2, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $fmt)
        [int]$stride = $bd1.Stride
        $bytes1 = New-Object byte[] ($stride * $h)
        $bytes2 = New-Object byte[] ($stride * $h)
        [System.Runtime.InteropServices.Marshal]::Copy($bd1.Scan0, $bytes1, 0, $bytes1.Length)
        [System.Runtime.InteropServices.Marshal]::Copy($bd2.Scan0, $bytes2, 0, $bytes2.Length)
        $bmp1.UnlockBits($bd1); $bmp2.UnlockBits($bd2)
        $bmp1.Dispose(); $bmp2.Dispose()
        [int]$maxDelta = 0; [int]$mismatch = 0
        for ([int]$y = 0; $y -lt $h; $y++) {
            for ([int]$x = 0; $x -lt $w; $x++) {
                [int]$i  = $y * $stride + $x * 4
                [int]$dB = [Math]::Abs([int]$bytes1[$i]   - [int]$bytes2[$i])
                [int]$dG = [Math]::Abs([int]$bytes1[$i+1] - [int]$bytes2[$i+1])
                [int]$dR = [Math]::Abs([int]$bytes1[$i+2] - [int]$bytes2[$i+2])
                [int]$d  = [Math]::Max($dR, [Math]::Max($dG, $dB))
                if ($d -gt $maxDelta) { $maxDelta = $d }
                if ($d -gt $TOL) { $mismatch++ }
            }
        }
        return @{ MaxDelta=$maxDelta; MismatchPixels=$mismatch; Misaligned=$false; SizeMsg=''; OK=$true }
    } catch {
        return @{ MaxDelta=999; MismatchPixels=-1; Misaligned=$false; SizeMsg=''; OK=$false; Err=$_.Exception.Message }
    }
}

function Make-B64Thumb([string]$path) {
    try {
        $src = [System.Drawing.Bitmap]::new($path)
        $th  = [System.Drawing.Bitmap]::new(128, 120)
        $gfx = [System.Drawing.Graphics]::FromImage($th)
        $gfx.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $gfx.DrawImage($src, 0, 0, 128, 120)
        $gfx.Dispose(); $src.Dispose()
        $ms = New-Object System.IO.MemoryStream
        $th.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $th.Dispose()
        return [Convert]::ToBase64String($ms.ToArray())
    } catch { return '' }
}

function Make-B64Full([string]$path) {
    try {
        $src = [System.Drawing.Bitmap]::new($path)
        $ms  = New-Object System.IO.MemoryStream
        $src.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $src.Dispose()
        return [Convert]::ToBase64String($ms.ToArray())
    } catch { return '' }
}

# ── Main comparison loop ──────────────────────────────────────────────────────
$frames = @()
for ($f = $Interval; $f -le $NFrames; $f += $Interval) { $frames += $f }

Write-Host "Comparing $($frames.Count) frame screenshots: nes1 vs FCEUX..."

$rows  = [System.Collections.Generic.List[PSCustomObject]]::new()
[int]$nPass=0; [int]$nFail=0; [int]$nMiss=0

foreach ($f in $frames) {
    $nes1Path  = Join-Path $Nes1Dir  ('nes1_frame_{0:D4}.bmp'  -f $f)
    $fceuxPath = Join-Path $FceuxDir ('fceux_frame_{0:D4}.bmp' -f $f)

    $nes1Exists  = Test-Path $nes1Path
    $fceuxExists = Test-Path $fceuxPath

    if (-not $nes1Exists -and -not $fceuxExists) {
        $status = 'MISS'; $nMiss++
        $rows.Add([PSCustomObject]@{
            Frame=$f; Status='MISS'; MaxDelta=999; MismatchPixels=-1
            Misaligned=$false; SizeMsg=''; Nes1Thumb=''; Nes1Full=''; FceuxThumb=''; FceuxFull=''
        }); continue
    }

    $nes1Thumb  = if ($nes1Exists)  { Make-B64Thumb $nes1Path  } else { '' }
    $nes1Full   = if ($nes1Exists)  { Make-B64Full  $nes1Path  } else { '' }
    $fceuxThumb = if ($fceuxExists) { Make-B64Thumb $fceuxPath } else { '' }
    $fceuxFull  = if ($fceuxExists) { Make-B64Full  $fceuxPath } else { '' }

    if (-not $nes1Exists -or -not $fceuxExists) {
        $status = 'MISS'; $nMiss++
        $rows.Add([PSCustomObject]@{
            Frame=$f; Status='MISS'; MaxDelta=999; MismatchPixels=-1
            Misaligned=$false; SizeMsg=''; Nes1Thumb=$nes1Thumb; Nes1Full=$nes1Full
            FceuxThumb=$fceuxThumb; FceuxFull=$fceuxFull
        }); continue
    }

    $cmp = Compare-Images $nes1Path $fceuxPath
    if (-not $cmp.OK) {
        $status = 'MISS'; $nMiss++
    } elseif ($cmp.Misaligned) {
        $status = 'FAIL'; $nFail++
        Write-Warning "Size mismatch at frame $f`: $($cmp.SizeMsg)"
    } else {
        $status = if ($cmp.MismatchPixels -eq 0) { 'PASS' } else { 'FAIL' }
        if ($status -eq 'PASS') { $nPass++ } else { $nFail++ }
    }

    $rows.Add([PSCustomObject]@{
        Frame=$f; Status=$status
        MaxDelta=$cmp.MaxDelta; MismatchPixels=$cmp.MismatchPixels
        Misaligned=$cmp.Misaligned; SizeMsg=$cmp.SizeMsg
        Nes1Thumb=$nes1Thumb; Nes1Full=$nes1Full
        FceuxThumb=$fceuxThumb; FceuxFull=$fceuxFull
    })

    Write-Host "  Frame $f`: $status  (d=$($cmp.MaxDelta), $($cmp.MismatchPixels) px)"
}

Write-Host "Done: Pass=$nPass  Fail=$nFail  Missing=$nMiss  Total=$($frames.Count)"

# ── HTML report ───────────────────────────────────────────────────────────────
[int]$total  = $frames.Count
[int]$rate   = if ($total -gt 0) { [int](100 * $nPass / $total) } else { 0 }
$badge       = if ($nFail -eq 0 -and $nMiss -eq 0) { 'ALL PASS' } else { "$nFail FAIL / $nMiss MISSING" }
$badgeColor  = if ($nFail -eq 0 -and $nMiss -eq 0) { '#2a9d8f' } else { '#e63946' }
$now         = Get-Date -Format 'yyyy-MM-dd HH:mm'

$detHtml = ''
foreach ($r in $rows) {
    $statusTd = switch ($r.Status) {
        'PASS' { '<td style="color:#2a9d8f">PASS</td>' }
        'FAIL' { '<td style="color:red;font-weight:bold">FAIL</td>' }
        default { '<td style="color:orange">MISS</td>' }
    }
    $deltaTd = if ($r.Misaligned) {
        '<td style="color:orange">SIZE MISMATCH<br><small>{0}</small></td>' -f $r.SizeMsg
    } elseif ($r.MismatchPixels -lt 0) {
        '<td></td>'
    } else {
        '<td>d={0}<br><small>{1} px</small></td>' -f $r.MaxDelta, $r.MismatchPixels
    }
    $nes1Td = if ($r.Nes1Thumb) {
        '<td><img src="data:image/png;base64,{0}" width="128" height="120" class="thumb" data-full="data:image/png;base64,{1}" style="cursor:zoom-in" title="nes1 - click to enlarge"></td>' -f $r.Nes1Thumb, $r.Nes1Full
    } else { '<td><small>MISSING</small></td>' }
    $fceuxTd = if ($r.FceuxThumb) {
        '<td><img src="data:image/png;base64,{0}" width="128" height="120" class="thumb" data-full="data:image/png;base64,{1}" style="cursor:zoom-in" title="FCEUX - click to enlarge"></td>' -f $r.FceuxThumb, $r.FceuxFull
    } else { '<td><small>MISSING</small></td>' }

    $rowBg = switch ($r.Status) { 'PASS' { '#1a2e1a' } 'FAIL' { '#2e1a1a' } default { '#2e2a1a' } }
    $detHtml += '<tr style="background:{0}">{1}<td>Frame {2}</td>{3}{4}{5}</tr>' -f $rowBg,$statusTd,$r.Frame,$deltaTd,$nes1Td,$fceuxTd
    $detHtml += "`n"
}

$css = @'
body{font-family:monospace;background:#1a1a2e;color:#e0e0e0;margin:0;padding:20px}
h1{color:#f0c040}
h2{color:#a0c0ff;border-bottom:1px solid #444;padding-bottom:4px;margin-top:32px}
.badge{display:inline-block;padding:6px 18px;border-radius:4px;font-size:1.1em;font-weight:bold;color:white}
table{border-collapse:collapse;margin-bottom:20px}
th{background:#2a2a4e;color:#f0c040;padding:6px 10px;text-align:left;position:sticky;top:0}
td{padding:4px 8px;border:1px solid #333;font-size:0.85em;vertical-align:middle}
.modal{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.85);z-index:1000;align-items:center;justify-content:center}
.modal img{image-rendering:pixelated;max-width:90vw;max-height:90vh}
'@

$html = @"
<!DOCTYPE html>
<html><head><meta charset="utf-8">
<title>Mappy Frame Test - nes1 vs FCEUX</title>
<style>$css</style></head>
<body>
<h1>Mappy (Japan) — Frame Test: nes1 vs FCEUX 2.6.6</h1>
<p>Generated: $now</p>
<p>Method: Run $NFrames frames from reset (no input), compare every $Interval frames pixel-by-pixel.
NES overscan: nes1=256&times;240 vs FCEUX=256&times;224; compared region rows 8&ndash;231 of nes1 vs rows 0&ndash;223 of FCEUX.
Tolerance: &plusmn;$TOL per channel.</p>
<p><span class="badge" style="background:$badgeColor">$badge</span>
&nbsp;&nbsp;Pass: <b>$nPass</b> &nbsp; Fail: <b>$nFail</b> &nbsp; Missing: <b>$nMiss</b> &nbsp; Total: <b>$total</b> &nbsp; Rate: <b>$rate%</b></p>

<h2>Frame Comparisons</h2>
<table>
<tr><th>Result</th><th>Frame</th><th>Delta</th><th>nes1 screenshot</th><th>FCEUX screenshot</th></tr>
$detHtml
</table>

<div class="modal" id="modal" onclick="this.style.display='none'">
  <img id="modal-img" src="">
</div>
<script>
document.querySelectorAll('.thumb').forEach(function(img){
  img.addEventListener('click',function(){
    var m=document.getElementById('modal');
    document.getElementById('modal-img').src=this.getAttribute('data-full');
    m.style.display='flex';
  });
});
</script>
</body></html>
"@

New-Item -ItemType Directory -Force (Split-Path $OutHtml) | Out-Null
$html | Out-File -FilePath $OutHtml -Encoding UTF8
Write-Host "Report written: $OutHtml"
Write-Host "RESULT: Pass=$nPass  Fail=$nFail  Missing=$nMiss  Total=$total  Rate=$rate%"
