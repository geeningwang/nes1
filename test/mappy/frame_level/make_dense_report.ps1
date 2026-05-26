param(
    [string]$Nes1Dir   = "C:\Work\nes1\test\mappy_out\nes1_dense_out",
    [string]$FceuxDir  = "C:\Work\nes1\test\mappy_out\fceux_dense_out",
    [string]$OutHtml   = "C:\Work\nes1\test\mappy_out\dense_report.html",
    [int]   $NFrames   = 270
)

Add-Type -AssemblyName System.Drawing

# ── Pixel comparison ──────────────────────────────────────────────────────────
function Compare-Bmp([string]$p1, [string]$p2) {
    try {
        $b1 = [System.Drawing.Bitmap]::new($p1)
        $b2 = [System.Drawing.Bitmap]::new($p2)
        $w  = [Math]::Min($b1.Width,  $b2.Width)
        $h  = [Math]::Min($b1.Height, $b2.Height)
        [int]$o1 = if ($b1.Height -gt $b2.Height) { ($b1.Height-$b2.Height)/2 } else { 0 }
        [int]$o2 = if ($b2.Height -gt $b1.Height) { ($b2.Height-$b1.Height)/2 } else { 0 }
        $fmt = [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
        $r1  = [System.Drawing.Rectangle]::new(0,$o1,$w,$h)
        $r2  = [System.Drawing.Rectangle]::new(0,$o2,$w,$h)
        $bd1 = $b1.LockBits($r1,[System.Drawing.Imaging.ImageLockMode]::ReadOnly,$fmt)
        $bd2 = $b2.LockBits($r2,[System.Drawing.Imaging.ImageLockMode]::ReadOnly,$fmt)
        $bytes1 = New-Object byte[] ($bd1.Stride*$h)
        $bytes2 = New-Object byte[] ($bd2.Stride*$h)
        [System.Runtime.InteropServices.Marshal]::Copy($bd1.Scan0,$bytes1,0,$bytes1.Length)
        [System.Runtime.InteropServices.Marshal]::Copy($bd2.Scan0,$bytes2,0,$bytes2.Length)
        $b1.UnlockBits($bd1); $b2.UnlockBits($bd2)
        $b1.Dispose(); $b2.Dispose()
        [int]$maxD=0; [int]$mis=0; [int]$stride=$bd1.Stride
        for ([int]$y=0;$y-lt$h;$y++) {
            for ([int]$x=0;$x-lt$w;$x++) {
                [int]$i=$y*$stride+$x*4
                [int]$d=[Math]::Max([Math]::Abs([int]$bytes1[$i]-[int]$bytes2[$i]),
                    [Math]::Max([Math]::Abs([int]$bytes1[$i+1]-[int]$bytes2[$i+1]),
                                [Math]::Abs([int]$bytes1[$i+2]-[int]$bytes2[$i+2])))
                if ($d -gt $maxD) { $maxD=$d }
                if ($d -gt 0)     { $mis++ }
            }
        }
        return @{ OK=$true; DiffPx=$mis; MaxD=$maxD }
    } catch { return @{ OK=$false; DiffPx=-1; MaxD=-1 } }
}

# ── Image → base64 PNG ────────────────────────────────────────────────────────
function Bmp-ToB64([string]$path) {
    try {
        $src = [System.Drawing.Bitmap]::new($path)
        $ms  = New-Object System.IO.MemoryStream
        $src.Save($ms,[System.Drawing.Imaging.ImageFormat]::Png)
        $src.Dispose()
        return [Convert]::ToBase64String($ms.ToArray())
    } catch { return '' }
}

# ── State comparison (txt files) ──────────────────────────────────────────────
function Compare-Txt([string]$pN, [string]$pE) {
    $r = [ordered]@{
        TxtPresent=1; TxtLengthOK=1; TxtIssues=@()
        CpuOK=1; CpuNes1=''; CpuFceux=''
        PcNes1=''; PcFceux=''
        CycleNes1=''; CycleFceux=''; CycleDelta=''; CycleOK=1
        PpuOK=1; PpuDiffs=@()
        PpuStatusOK=1; PpuStatusNes1=''; PpuStatusFceux=''
        PalOK=1; PalNes1=''; PalFceux=''
        NtOK=1;  NtDiffs=0
        OamOK=1; OamDiffs=@()
        LoopyOK=1; LoopyNes1=''; LoopyFceux=''
        ScrollOK=1; ScrollNes1=''; ScrollFceux=''
        RamOK=1; RamDiffs=0; RamPresent=$false
    }
    if (!(Test-Path $pN) -or !(Test-Path $pE)) {
        $r.TxtPresent = 0
        $r.TxtIssues += "missing txt: nes1=$([int](Test-Path $pN)) fceux=$([int](Test-Path $pE))"
        return $r
    }
    $nl = Get-Content $pN
    $el = Get-Content $pE
    if ($nl.Count -ne $el.Count) {
        $r.TxtLengthOK = 0
        $r.TxtIssues += "line count differs: nes1=$($nl.Count) fceux=$($el.Count)"
    }
    $cpuLineN = $nl | Select-String '^PC=' | Select-Object -First 1
    $cpuLineE = $el | Select-String '^PC=' | Select-Object -First 1
    if ($cpuLineN -and $cpuLineE) {
        $r.PcNes1 = [string]$cpuLineN.Line
        $r.PcFceux = [string]$cpuLineE.Line
        if ($r.PcNes1 -match 'cycles=(\d+)') { $r.CycleNes1 = [long]$Matches[1] }
        if ($r.PcFceux -match 'cycles=(\d+)') { $r.CycleFceux = [long]$Matches[1] }
        if (($r.CycleNes1 -ne '') -and ($r.CycleFceux -ne '')) {
            $r.CycleDelta = [long]$r.CycleNes1 - [long]$r.CycleFceux
            $r.CycleOK = ([long]$r.CycleDelta -eq 0)
        }
    } elseif ($cpuLineN -or $cpuLineE) {
        $r.CpuOK = 0
        $r.CpuNes1 = if ($cpuLineN) { [string]$cpuLineN.Line } else { '<missing CPU line>' }
        $r.CpuFceux = if ($cpuLineE) { [string]$cpuLineE.Line } else { '<missing CPU line>' }
        $r.CycleOK = 0
        $r.TxtIssues += 'CPU line missing on one side'
    }
    # Detect whether both files contain a RAM section (new-format txt only)
    $r.RamPresent = (($nl -match '=== CPU RAM') -and ($el -match '=== CPU RAM'))
    $section = ''
    $len = [Math]::Min($nl.Count,$el.Count)
    for ($i=0;$i-lt$len;$i++) {
        $n=$nl[$i]; $e=$el[$i]
        # Track current section from headers (always identical, so check before eq-skip)
        if ($n -match '^=== ') {
            if      ($n -match 'CPU RAM')   { $section = 'RAM' }
            elseif  ($n -match 'Nametable') { $section = 'NT'  }
            else                            { $section = ''     }
            continue
        }
        if ($n -eq $e) { continue }
        if ($n -match '^PC=') {
            $r.CpuOK=0; $r.CpuNes1=$n; $r.CpuFceux=$e
        } elseif ($n -match '^PPUSTATUS') {
            $r.PpuStatusOK=0; $r.PpuStatusNes1=$n; $r.PpuStatusFceux=$e
        } elseif ($n -match '^PPUCTRL|^PPUMASK') {
            $r.PpuOK=0; $r.PpuDiffs += "$($n.Split(' ')[0])|nes1=$n|fceux=$e"
        } elseif ($n -match '^v=|^render_scroll') {
            if ($n -match '^v=') {
                $r.LoopyNes1=$n; $r.LoopyFceux=$e
                if ($n -ne $e) { $r.LoopyOK=0 }
            } else {
                $r.ScrollNes1=$n; $r.ScrollFceux=$e
                if ($n -ne $e) { $r.ScrollOK=0 }
            }
        } elseif ($n -match '^BG:|^SPR:') {
            $r.PalOK=0; $r.PalNes1+="$n  "; $r.PalFceux+="$e  "
        } elseif ($n -match '^[0-9A-F]{2} ') {
            if ($section -eq 'RAM') { $r.RamOK=0; $r.RamDiffs++ }
            else { $r.NtOK=0; $r.NtDiffs++ }
        } elseif ($n -match '^\s+\d+\s+\|') {
            # Sprite data line: "  5 | 208 194   05   40"
            $r.OamOK=0; $r.OamDiffs += "nes1:[$n] fceux:[$e]"
        } elseif ($n -match 'Y=\d+|OAM') {
            $r.OamOK=0; $r.OamDiffs += $n
        }
    }
    return $r
}

# ── HTML helpers ──────────────────────────────────────────────────────────────
function HE([string]$s) { $s -replace '&','&amp;' -replace '<','&lt;' -replace '>','&gt;' }

# ── Main loop ─────────────────────────────────────────────────────────────────
$rows = [System.Collections.Generic.List[hashtable]]::new()
$nPass=0; $nFail=0
[int]$nCycleDiff = 0

Write-Host "Generating dense report for $NFrames frames..."

for ($f=1; $f-le$NFrames; $f++) {
    $pN_bmp = Join-Path $Nes1Dir  ("nes1_frame_{0:D4}.bmp"  -f $f)
    $pE_bmp = Join-Path $FceuxDir ("fceux_frame_{0:D4}.bmp" -f $f)
    $pN_txt = Join-Path $Nes1Dir  ("nes1_frame_{0:D4}.txt"  -f $f)
    $pE_txt = Join-Path $FceuxDir ("fceux_frame_{0:D4}.txt" -f $f)

    $pix = if ((Test-Path $pN_bmp) -and (Test-Path $pE_bmp)) {
        Compare-Bmp $pN_bmp $pE_bmp
    } else { @{ OK=$false; DiffPx=-1; MaxD=-1 } }

    $st = Compare-Txt $pN_txt $pE_txt

    $b64N = if (Test-Path $pN_bmp) { Bmp-ToB64 $pN_bmp } else { '' }
    $b64E = if (Test-Path $pE_bmp) { Bmp-ToB64 $pE_bmp } else { '' }

    $pixPass = $pix.OK -and ($pix.DiffPx -eq 0)
    $stPass  = $st.TxtPresent -and $st.TxtLengthOK -and $st.CpuOK -and $st.CycleOK -and $st.PpuOK -and $st.PpuStatusOK -and $st.PalOK -and $st.NtOK -and $st.OamOK -and $st.LoopyOK -and $st.ScrollOK -and (-not $st.RamPresent -or $st.RamOK)
    $allPass = $pixPass -and $stPass

    if ($allPass) { $nPass++ } else { $nFail++ }
    if (-not $st.CycleOK) { $nCycleDiff++ }

    $rows.Add(@{
        F=$f; PixPass=$pixPass; StPass=$stPass; AllPass=$allPass
        DiffPx=$pix.DiffPx; MaxD=$pix.MaxD
        St=$st; B64N=$b64N; B64E=$b64E
    })

    if ($f % 30 -eq 0) { Write-Host "  Frame $f / $NFrames" }
}

Write-Host "Done: Pass=$nPass  Fail=$nFail"

# ── Build HTML rows ───────────────────────────────────────────────────────────
$sb = [System.Text.StringBuilder]::new()

foreach ($r in $rows) {
    $f    = $r.F
    $st   = $r.St
    $ap   = $r.AllPass
    $pp   = $r.PixPass
    $sp   = $r.StPass

    # Row background
    $bg   = if ($ap) { '#111e11' } else { '#2a1010' }

    # Overall badge
    $badge = if ($ap) { '<span class="ok">PASS</span>' } else { '<span class="fail">FAIL</span>' }

    # Pixel cell
    $pixCell = if ($r.DiffPx -lt 0) {
        '<span style="color:#888">N/A</span>'
    } elseif ($r.DiffPx -eq 0) {
        '<span class="ok">0 diff</span>'
    } else {
        '<span class="fail">{0} px (max&Delta;={1})</span>' -f $r.DiffPx,$r.MaxD
    }

    $cpuIssues = @($st.TxtIssues)
    $cpuIssueHtml = if ($cpuIssues.Count -gt 0) {
        '<br><code class="d">{0}</code>' -f (HE ($cpuIssues -join '; '))
    } else { '' }
    $cpuCell = if ($st.CpuOK -and $st.TxtPresent -and $st.TxtLengthOK) {
        '<span class="ok">OK</span><br><code class="inf">{0}</code>' -f (HE $st.PcNes1)
    } else {
        '<span class="fail">DIFF</span><br><code class="d">{0}</code><br><code class="d2">{1}</code>{2}' -f (HE $st.CpuNes1),(HE $st.CpuFceux),$cpuIssueHtml
    }

    $tickCell = if ($st.CycleNes1 -eq '' -or $st.CycleFceux -eq '') {
        '<span style="color:#888">N/A</span>'
    } elseif ($st.CycleOK) {
        '<span class="ok">match</span><br><code class="inf">cycles={0}</code>' -f $st.CycleNes1
    } else {
        '<span class="fail">DIFF</span><br><code class="d">nes1:{0}</code><br><code class="d2">fceux:{1}</code><br><code class="inf">delta:{2}</code>' -f $st.CycleNes1,$st.CycleFceux,$st.CycleDelta
    }

    # PPU cell — PPUCTRL, PPUMASK, and PPUSTATUS
    $ppuLines = ''
    foreach ($d in $st.PpuDiffs) {
        $parts = $d -split '\|'
        $ppuLines += '<tr><td class="lbl">{0}</td><td class="d">{1}</td><td class="d2">{2}</td></tr>' -f (HE $parts[0]),(HE ($parts[1] -replace '^nes1=')),((HE ($parts[2] -replace '^fceux=')))
    }
    if (-not $st.PpuStatusOK) {
        $ppuLines += '<tr><td class="lbl">PPUSTAT</td><td class="d">{0}</td><td class="d2">{1}</td></tr>' -f (HE $st.PpuStatusNes1),(HE $st.PpuStatusFceux)
    }
    $ppuCell = if ($st.PpuOK) { 
        if ($st.PpuStatusOK) { '<span class="ok">OK</span>' }
        else { '<table class="sub">{0}</table>' -f $ppuLines }
    } else {
        '<table class="sub">{0}</table>' -f $ppuLines
    }

    # Palette cell
    $palCell = if ($st.PalOK) { '<span class="ok">OK</span>' } else {
        '<code class="d">{0}</code><br><code class="d2">{1}</code>' -f (HE $st.PalNes1.Trim()),(HE $st.PalFceux.Trim())
    }

    # NT cell
    $ntCell = if ($st.NtOK) { '<span class="ok">OK</span>' } else {
        '<span class="fail">{0} tile diffs</span>' -f $st.NtDiffs
    }

    # OAM cell
    $oamCell = if ($st.OamOK) { '<span class="ok">OK</span>' } else {
        '<span class="fail">DIFF</span><br><code class="d">{0}</code>' -f (HE ($st.OamDiffs -join '; '))
    }

    # V/T cell — exact compare in strict mode
    $loopyPart = if ($st.LoopyNes1) {
        $lc1 = if ($st.LoopyOK) { 'inf' } else { 'd' }
        $lc2 = if ($st.LoopyOK) { 'inf2' } else { 'd2' }
        '<code class="{0}">nes1:{1}</code><br><code class="{2}">fceux:{3}</code>' -f $lc1,(HE $st.LoopyNes1),$lc2,(HE $st.LoopyFceux)
    } else { '' }
    $scrollPart = if ($st.ScrollNes1) {
        $sc1 = if ($st.ScrollOK) { 'inf' } else { 'd' }
        $sc2 = if ($st.ScrollOK) { 'inf2' } else { 'd2' }
        '<br><code class="{0}">{1}</code><br><code class="{2}">{3}</code>' -f $sc1,(HE $st.ScrollNes1),$sc2,(HE $st.ScrollFceux)
    } else { '' }
    $scrollCell = if ($loopyPart -or $scrollPart) { $loopyPart + $scrollPart } else { '<span class="ok">same</span>' }

    # Images
    $imgN = if ($r.B64N) {
        '<img src="data:image/png;base64,{0}" class="scr" title="NES1 F{1}">' -f $r.B64N,$f
    } else { '<small>MISSING</small>' }
    $imgE = if ($r.B64E) {
        '<img src="data:image/png;base64,{0}" class="scr" title="FCEUX F{1}">' -f $r.B64E,$f
    } else { '<small>MISSING</small>' }

    # RAM cell
    $ramCell = if (-not $st.RamPresent) {
        '<span style="color:#888">N/A</span>'
    } elseif ($st.RamOK) {
        '<span class="ok">OK</span>'
    } else {
        '<span class="fail">{0} row diffs</span>' -f $st.RamDiffs
    }

    $null = $sb.AppendLine(
        "<tr style=`"background:$bg`"><td>$badge</td><td><b>F$f</b></td><td>$pixCell</td><td>$cpuCell</td><td>$tickCell</td><td>$ppuCell</td><td>$palCell</td><td>$ntCell</td><td>$oamCell</td><td>$ramCell</td><td>$scrollCell</td><td>$imgN</td><td>$imgE</td></tr>"
    )
}

$tableBody = $sb.ToString()

$now   = Get-Date -Format 'yyyy-MM-dd HH:mm'
$total = $rows.Count
$rate  = if ($total) { [int](100*$nPass/$total) } else { 0 }
$badgeHead = if ($nFail -eq 0) { 'ALL PASS' } else { "$nFail FAIL" }
$badgeColor = if ($nFail -eq 0) { '#2a9d8f' } else { '#e63946' }

$html = @"
<!DOCTYPE html><html><head><meta charset="utf-8">
<title>Mappy Dense Report — nes1 vs FCEUX</title>
<style>
body{font-family:monospace;background:#111;color:#ddd;margin:0;padding:16px;font-size:13px}
h1{color:#f0c040;margin:0 0 4px 0}
.badge{display:inline-block;padding:4px 14px;border-radius:3px;font-weight:bold;color:#fff}
table{border-collapse:collapse;width:100%;margin-top:12px}
th{background:#1e1e3a;color:#f0c040;padding:5px 8px;text-align:left;position:sticky;top:0;z-index:2;white-space:nowrap}
td{padding:3px 7px;border:1px solid #2a2a2a;vertical-align:top;font-size:12px}
.ok{color:#4caf50;font-weight:bold}
.fail{color:#f44336;font-weight:bold}
code.d{color:#ff8a65;display:block;white-space:pre-wrap;word-break:break-all;font-size:11px}
code.d2{color:#80cbc4;display:block;white-space:pre-wrap;word-break:break-all;font-size:11px}
code.inf{color:#aaa;display:block;font-size:10px}
code.inf2{color:#78909c;display:block;font-size:10px}
table.sub{border:none;background:transparent;margin:0}
table.sub td{border:none;padding:1px 3px;font-size:11px}
td.lbl{color:#f0c040;font-size:10px;white-space:nowrap}
.scr{display:block;image-rendering:pixelated;width:128px;height:120px;cursor:zoom-in;border:1px solid #333}
.modal{display:none;position:fixed;inset:0;background:rgba(0,0,0,.9);z-index:100;align-items:center;justify-content:center}
.modal img{image-rendering:pixelated;max-width:95vw;max-height:95vh;cursor:zoom-out}
</style></head>
<body>
<h1>Mappy (Japan) -- Dense Frame Report: nes1 vs FCEUX</h1>
<p>Generated: $now &nbsp;|&nbsp; Frames: 1-$NFrames (every frame) &nbsp;|&nbsp;
Pass: <b>$nPass</b> &nbsp; Fail: <b>$nFail</b> &nbsp; Rate: <b>$rate%</b> &nbsp;
Tick diffs: <b>$nCycleDiff</b> &nbsp; <span class="badge" style="background:$badgeColor">$badgeHead</span></p>
<p style="color:#888;font-size:11px">
Pixel: NES1 256&times;240 vs FCEUX 256&times;240 compared center-aligned, tolerance=0 (exact match).<br>
Text dump presence and line count: fail if differ. &nbsp;
CPU line (PC/A/X/Y/S/P) and extracted ticks/cycles: fail if differ. &nbsp;
PPUCTRL/PPUMASK/PPUSTATUS: fail if differ. &nbsp;
V/T register and render_scroll: fail if differ. &nbsp;
NT, Palette, OAM sprite data, CPU RAM ($0000-$07FF): fail if differ (N/A if txt predates RAM export).
</p>

<table>
<tr>
  <th>Result</th><th>Frame</th>
  <th>Pixels</th>
  <th>CPU (PC/A/X/Y/S/P)</th>
  <th>Ticks / cycles</th>
  <th>PPU regs / status</th>
  <th>Palette ($3F00-$3F1F)</th>
  <th>Nametable</th>
  <th>OAM</th>
  <th>RAM ($0000-$07FF)</th>
  <th>V/T &amp; render_scroll</th>
  <th>NES1</th><th>FCEUX</th>
</tr>
$tableBody
</table>

<div class="modal" id="modal" onclick="this.style.display='none'">
  <img id="mimg" src="">
</div>
<script>
document.querySelectorAll('.scr').forEach(function(img){
  img.addEventListener('click',function(e){
    e.stopPropagation();
    document.getElementById('mimg').src=this.src;
    document.getElementById('modal').style.display='flex';
  });
});
</script>
</body></html>
"@

[System.IO.File]::WriteAllText($OutHtml, $html, [System.Text.Encoding]::UTF8)
Write-Host "Report written to: $OutHtml"
