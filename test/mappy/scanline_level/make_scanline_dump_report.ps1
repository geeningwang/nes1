param(
    [string]$Nes1Dir  = "C:\Work\nes1\test\mappy_out\nes1_sl_dump_f1",
    [string]$FceuxDir = "C:\Work\nes1\test\mappy_out\fceux_sl_dump_f1",
    [string]$OutHtml  = "C:\Work\nes1\test\mappy_out\f1_compare.html",
    [int]$Frame       = 1
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

function Compare-BmpPixels([string]$nes1Path, [string]$fceuxPath) {
    if (-not (Test-Path $nes1Path) -or -not (Test-Path $fceuxPath)) {
        return @{ OK=$false; DiffPx=-1; MaxD=-1 }
    }

    $b1 = [System.Drawing.Bitmap]::new($nes1Path)
    $b2 = [System.Drawing.Bitmap]::new($fceuxPath)
    try {
        $w = [Math]::Min($b1.Width, $b2.Width)
        $h = [Math]::Min($b1.Height, $b2.Height)
        [int]$diff = 0
        [int]$maxD = 0
        for ([int]$y = 0; $y -lt $h; ++$y) {
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
        return @{ OK=$true; DiffPx=$diff; MaxD=$maxD }
    } finally {
        $b1.Dispose()
        $b2.Dispose()
    }
}

function Image-ToDataUri([string]$path) {
    if (-not (Test-Path $path)) { return '' }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    return 'data:image/bmp;base64,' + [Convert]::ToBase64String($bytes)
}

function HE([string]$s) {
    if ($null -eq $s) { return '' }
    return [System.Net.WebUtility]::HtmlEncode($s)
}

$rows = [System.Collections.Generic.List[hashtable]]::new()
[int]$textDiffs = 0
[int]$bmpDiffs = 0
$nes1Status = Get-DumpStatus $Nes1Dir
$fceuxStatus = Get-DumpStatus $FceuxDir
$unavailable = ($nes1Status.Status -ne 'valid' -or $fceuxStatus.Status -ne 'valid')

if (-not $unavailable) {
    for ([int]$sl = 0; $sl -lt 240; ++$sl) {
        $nTxt = Join-Path $Nes1Dir  ("nes1_frame_{0:D4}_sl_{1:D3}.txt" -f $Frame, $sl)
        $fTxt = Join-Path $FceuxDir ("fceux_frame_{0:D4}_sl_{1:D3}.txt" -f $Frame, $sl)
        $nBmp = Join-Path $Nes1Dir  ("nes1_frame_{0:D4}_sl_{1:D3}.bmp" -f $Frame, $sl)
        $fBmp = Join-Path $FceuxDir ("fceux_frame_{0:D4}_sl_{1:D3}.bmp" -f $Frame, $sl)

        $nText = Get-NormalizedText $nTxt
        $fText = Get-NormalizedText $fTxt
        $textOK = ($null -ne $nText) -and ($nText -eq $fText)
        if (-not $textOK) { ++$textDiffs }

        $bmp = Compare-BmpPixels $nBmp $fBmp
        $bmpOK = $bmp.OK -and ($bmp.DiffPx -eq 0)
        if (-not $bmpOK) { ++$bmpDiffs }

        $rows.Add(@{
            SL=$sl
            TextOK=$textOK
            BmpOK=$bmpOK
            DiffPx=$bmp.DiffPx
            MaxD=$bmp.MaxD
            Nes1Bmp=$nBmp
            FceuxBmp=$fBmp
        })
    }
}

$now = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
$allOK = (-not $unavailable) -and ($textDiffs -eq 0 -and $bmpDiffs -eq 0)
$badgeClass = if ($unavailable) { 'warn' } elseif ($allOK) { 'ok' } else { 'bad' }
$badgeText = if ($unavailable) { 'SCANLINE TRACE UNAVAILABLE' } elseif ($allOK) { 'ALL PASS' } else { "$textDiffs text diffs / $bmpDiffs bmp diffs" }

$rowHtml = [System.Text.StringBuilder]::new()
if (-not $unavailable) {
    foreach ($r in $rows) {
        $statusOK = $r.TextOK -and $r.BmpOK
        $trClass = if ($statusOK) { 'okrow' } else { 'badrow' }
        $txtCell = if ($r.TextOK) { '<span class="ok">OK</span>' } else { '<span class="bad">DIFF</span>' }
        $bmpCell = if ($r.BmpOK) {
            '<span class="ok">0 diff</span>'
        } elseif ($r.DiffPx -lt 0) {
            '<span class="bad">missing</span>'
        } else {
            '<span class="bad">{0} px, maxD={1}</span>' -f $r.DiffPx, $r.MaxD
        }
        $nImg = Image-ToDataUri $r.Nes1Bmp
        $fImg = Image-ToDataUri $r.FceuxBmp
        $null = $rowHtml.AppendFormat(
            '<tr class="{0}" data-diff="{1}"><td>SL {2:D3}</td><td>{3}</td><td>{4}</td><td><img class="thumb" src="{5}" data-full="{5}" title="NES1 SL {2:D3}"></td><td><img class="thumb" src="{6}" data-full="{6}" title="FCEUX SL {2:D3}"></td></tr>',
            $trClass, ($(if ($statusOK) { '0' } else { '1' })), $r.SL, $txtCell, $bmpCell, $nImg, $fImg)
        $null = $rowHtml.AppendLine()
    }
}

$css = @'
body{font-family:Consolas,monospace;background:#111;color:#ddd;margin:0;padding:16px;font-size:13px}
h1{color:#f0c040;margin:0 0 6px}
.meta{color:#aaa;margin-bottom:12px}
.badge{display:inline-block;padding:4px 12px;border-radius:3px;color:#fff;font-weight:bold}
.badge.ok,.ok{color:#9ece6a}.badge.ok{background:#214621;color:#fff}
.badge.bad,.bad{color:#f7768e}.badge.bad{background:#652525;color:#fff}
.badge.warn,.warn{color:#e0af68}.badge.warn{background:#5c4517;color:#fff}
.controls{margin:12px 0;color:#bbb}
label{cursor:pointer}
table{border-collapse:collapse;width:100%}
th{position:sticky;top:0;background:#24283b;color:#f0c040;padding:5px 8px;text-align:left;z-index:2}
td{border:1px solid #2a2a2a;padding:4px 7px;vertical-align:middle}
tr.okrow{background:#111e11}tr.badrow{background:#2a1010}
.thumb{width:128px;height:120px;image-rendering:pixelated;border:1px solid #333;cursor:zoom-in}
.modal{display:none;position:fixed;inset:0;background:rgba(0,0,0,.86);align-items:center;justify-content:center;z-index:20}
.modal img{max-width:95vw;max-height:95vh;image-rendering:pixelated}
.status-box{margin:16px 0;padding:12px;border:1px solid #5c4517;background:#1d1708;color:#e0af68}
'@

$bodyHtml = if ($unavailable) {
@"
<div class="status-box">
<div>NES1: $(HE $nes1Status.Status) $(HE $nes1Status.Reason)</div>
<div>FCEUX: $(HE $fceuxStatus.Status) $(HE $fceuxStatus.Reason)</div>
</div>
"@
} else {
@"
<div class="controls"><label><input type="checkbox" id="diffOnly" onchange="toggleDiffs()"> show diffs only</label></div>
<table>
<thead><tr><th>Scanline</th><th>Text</th><th>BMP</th><th>NES1</th><th>FCEUX</th></tr></thead>
<tbody>
$rowHtml
</tbody>
</table>
<div class="modal" id="modal" onclick="this.style.display='none'"><img id="modal-img" src=""></div>
"@
}

$html = @"
<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Frame $Frame Scanline Dump Compare</title>
<style>$css</style></head>
<body>
<h1>Mappy Frame $Frame Scanline Dump Compare</h1>
<div class="meta">Generated: $(HE $now) | NES1: $(HE $Nes1Dir) | FCEUX: $(HE $FceuxDir)</div>
<div><span class="badge $badgeClass">$badgeText</span> &nbsp; Text diffs: <b>$textDiffs</b> / 240 &nbsp; BMP diffs: <b>$bmpDiffs</b> / 240</div>
$bodyHtml
<script>
function toggleDiffs(){
  var only=document.getElementById('diffOnly').checked;
  document.querySelectorAll('tbody tr').forEach(function(row){
    row.style.display = (only && row.getAttribute('data-diff') === '0') ? 'none' : '';
  });
}
document.querySelectorAll('.thumb').forEach(function(img){
  img.addEventListener('click', function(){
    document.getElementById('modal-img').src = this.getAttribute('data-full');
    document.getElementById('modal').style.display = 'flex';
  });
});
</script>
</body></html>
"@

[System.IO.File]::WriteAllText($OutHtml, $html, [System.Text.Encoding]::UTF8)
Write-Host "Report written to: $OutHtml"
if ($unavailable) {
    Write-Host "Status: unavailable"
    Write-Host "NES1:  $($nes1Status.Status)  $($nes1Status.Reason)"
    Write-Host "FCEUX: $($fceuxStatus.Status)  $($fceuxStatus.Reason)"
} else {
    Write-Host "Text diffs: $textDiffs / 240"
    Write-Host "BMP diffs:  $bmpDiffs / 240"
}
