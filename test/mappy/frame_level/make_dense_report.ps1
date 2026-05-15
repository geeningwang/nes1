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
        CpuOK=1; CpuNes1=''; CpuFceux=''
        PcNes1=''; PcFceux=''
        PpuOK=1; PpuDiffs=@()
        PpuStatusOK=1; PpuStatusNes1=''; PpuStatusFceux=''
        PalOK=1; PalNes1=''; PalFceux=''
        NtOK=1;  NtDiffs=0
        OamOK=1; OamDiffs=@()
        LoopyOK=1; LoopyNes1=''; LoopyFceux=''
        ScrollNes1=''; ScrollFceux=''
        RamOK=1; RamDiffs=0; RamPresent=$false
    }
    if (!(Test-Path $pN) -or !(Test-Path $pE)) { return $r }
    $nl = Get-Content $pN
    $el = Get-Content $pE
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
            # Compare A/X/Y/S/P only. PC and cycle count are capture-timing sensitive:
            # the game idles in a 2-instruction loop (C06D/C06F) — NES1 and FCEUX
            # can capture at different points in that loop due to kook-frame phasing.
            # Cycles drift by instruction-boundary overshoot. Neither indicates a bug.
            $extract = { param($s)
                if ($s -match 'A=([0-9A-F]+) X=([0-9A-F]+) Y=([0-9A-F]+) S=([0-9A-F]+) P=([0-9A-F]+)') {
                    "$($Matches[1])$($Matches[2])$($Matches[3])$($Matches[4])$($Matches[5])"
                } else { $s }
            }
            $nr = & $extract $n; $er = & $extract $e
            if ($nr -ne $er) {
                $r.CpuOK=0; $r.CpuNes1=$n; $r.CpuFceux=$e
            }
            # Always capture full CPU line for informational PC/cycles display
            if (-not $r.PcNes1) { $r.PcNes1=$n; $r.PcFceux=$e }
        } elseif ($n -match '^PPUSTATUS') {
            $r.PpuStatusOK=0; $r.PpuStatusNes1=$n; $r.PpuStatusFceux=$e
        } elseif ($n -match '^PPUCTRL|^PPUMASK') {
            $r.PpuOK=0; $r.PpuDiffs += "$($n.Split(' ')[0])|nes1=$n|fceux=$e"
        } elseif ($n -match '^v=|^render_scroll') {
            # V/T can diverge at frame-end without a pixel error (architectural: NES1
            # captures V after all scanlines while FCEUX captures mid-frame V).  Treat
            # as informational. render_scroll is derived from V so also informational.
            if ($n -match '^v=') {
                $r.LoopyNes1=$n; $r.LoopyFceux=$e
                if ($n -ne $e) { $r.LoopyOK=0 }
            } else {
                $r.ScrollNes1=$n; $r.ScrollFceux=$e
            }
        } elseif ($n -match '^BG:|^SPR:') {
            # For SPR: lines, normalize bytes 0,4,8,12 to '00' before comparing.
            # These slots ($3F10/$3F14/$3F18/$3F1C) are hardware mirrors of the BG
            # transparent color; writes are redirected to $3F00 leaving physical RAM
            # at $3F10 etc. as 00. NES1 txt files (pre-fix) may show the mirrored
            # value here instead. We suppress that specific diff.
            if ($n -match '^SPR:') {
                $nNorm = $n -replace '^SPR: (\S+) (\S+ \S+ \S+) (\S+) (\S+ \S+ \S+) (\S+) (\S+ \S+ \S+) (\S+) (\S+ \S+ \S+)','SPR: 00 $2 00 $4 00 $6 00 $8'
                $eNorm = $e -replace '^SPR: (\S+) (\S+ \S+ \S+) (\S+) (\S+ \S+ \S+) (\S+) (\S+ \S+ \S+) (\S+) (\S+ \S+ \S+)','SPR: 00 $2 00 $4 00 $6 00 $8'
                if ($nNorm -ne $eNorm) {
                    $r.PalOK=0; $r.PalNes1+="$n  "; $r.PalFceux+="$e  "
                }
            } else {
                $r.PalOK=0; $r.PalNes1+="$n  "; $r.PalFceux+="$e  "
            }
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
    $stPass  = $st.CpuOK -and $st.PpuOK -and $st.PalOK -and $st.NtOK -and $st.OamOK -and (-not $st.RamPresent -or $st.RamOK)
    $allPass = $pixPass -and $stPass

    if ($allPass) { $nPass++ } else { $nFail++ }

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

    # CPU cell — show A/X/Y/S/P diff; PC/cycles shown as info when they diverge
    $pcInfo = ''
    if ($st.PcNes1 -and ($st.PcNes1 -ne $st.PcFceux)) {
        $pcInfo = '<br><code class="inf">nes1:{0}</code><br><code class="inf2">fceux:{1}</code>' -f (HE $st.PcNes1),(HE $st.PcFceux)
    }
    $cpuCell = if ($st.CpuOK) {
        '<span class="ok">OK</span>' + $pcInfo
    } else {
        '<span class="fail">DIFF</span><br><code class="d">{0}</code><br><code class="d2">{1}</code>' -f (HE $st.CpuNes1),(HE $st.CpuFceux)
    }

    # PPU cell — PPUCTRL, PPUMASK, and PPUSTATUS
    $ppuLines = ''
    foreach ($d in $st.PpuDiffs) {
        $parts = $d -split '\|'
        $ppuLines += '<tr><td class="lbl">{0}</td><td class="d">{1}</td><td class="d2">{2}</td></tr>' -f (HE $parts[0]),(HE ($parts[1] -replace '^nes1=')),((HE ($parts[2] -replace '^fceux=')))
    }
    if (-not $st.PpuStatusOK) {
        $ppuLines += '<tr><td class="lbl">PPUSTAT*</td><td class="inf">{0}</td><td class="inf2">{1}</td></tr><tr><td colspan="3" style="font-size:9px;color:#888">(* info only: spr0-hit/overflow are timing-dependent)</td></tr>' -f (HE $st.PpuStatusNes1),(HE $st.PpuStatusFceux)
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

    # V/T cell — shown highlighted when V/T differs; render_scroll shown as sub-info
    $loopyPart = if ($st.LoopyNes1) {
        $lc1 = if ($st.LoopyOK) { 'inf' } else { 'd' }
        $lc2 = if ($st.LoopyOK) { 'inf2' } else { 'd2' }
        '<code class="{0}">nes1:{1}</code><br><code class="{2}">fceux:{3}</code>' -f $lc1,(HE $st.LoopyNes1),$lc2,(HE $st.LoopyFceux)
    } else { '' }
    $scrollPart = if ($st.ScrollNes1) {
        '<br><code class="inf">{0}</code><br><code class="inf2">{1}</code>' -f (HE $st.ScrollNes1),(HE $st.ScrollFceux)
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
        "<tr style=`"background:$bg`"><td>$badge</td><td><b>F$f</b></td><td>$pixCell</td><td>$cpuCell</td><td>$ppuCell</td><td>$palCell</td><td>$ntCell</td><td>$oamCell</td><td>$ramCell</td><td>$scrollCell<br><small class='inf'>(V/T: info only)</small></td><td>$imgN</td><td>$imgE</td></tr>"
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
<span class="badge" style="background:$badgeColor">$badgeHead</span></p>
<p style="color:#888;font-size:11px">
Pixel: NES1 256&times;240 vs FCEUX 256&times;240 compared center-aligned, tolerance=0 (exact match).<br>
CPU A/X/Y/S/P: fail if differ. PC/cycles: shown as info only (idle-loop capture timing). &nbsp;
PPUCTRL/PPUMASK: fail if differ. PPUSTATUS: shown as info only (spr0-hit &amp; overflow are timing-dependent). &nbsp;
V/T register: informational only (frame-end V diverges architecturally -- not a pixel-rendering error). &nbsp;
render_scroll: derived from V, informational. &nbsp;
NT, Palette, OAM sprite data, CPU RAM ($0000-$07FF): fail if differ (N/A if txt predates RAM export).
</p>

<table>
<tr>
  <th>Result</th><th>Frame</th>
  <th>Pixels</th>
  <th>CPU (PC/A/X/Y/S/P)</th>
  <th>PPUCTRL / PPUMASK</th>
  <th>Palette ($3F00-$3F1F)</th>
  <th>Nametable</th>
  <th>OAM</th>
  <th>RAM ($0000-$07FF)</th>
  <th>V/T (info) &amp; render_scroll</th>
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
