# compare_f2_trace.ps1
# Compare nes1 and FCEUX per-scanline / per-write traces for Frame 2
# of Mappy (Japan) - comprehensive comparison of all CPU+PPU state.

$RomPath    = 'C:\Work\nes1\test\Mappy (Japan).nes'
$Nes1Exe    = 'C:\Work\nes1\src\x64\Debug\nes1.exe'
$OutDir     = 'C:\Work\nes1\test\mappy_out'
$Nes1Trace  = "$OutDir\nes1_sl_f2.txt"
$FceuxTrace = "$OutDir\fceux_sl_f2.txt"
$OutHtml    = "$OutDir\f2_compare.html"
$TargetFrame = 2

Write-Host "Running nes1 --scanlinetrace for frame $TargetFrame ..."
$null = New-Item -ItemType Directory -Path $OutDir -Force
& $Nes1Exe $RomPath --scanlinetrace $Nes1Trace $TargetFrame
if (-not (Test-Path $Nes1Trace)) { Write-Error "nes1 trace not generated: $Nes1Trace"; exit 1 }
Write-Host "  -> $Nes1Trace"

if (-not (Test-Path $FceuxTrace)) {
    Write-Warning "FCEUX trace not found: $FceuxTrace -- run mappy_f2_trace.lua in FCEUX first."
}

function Parse-TraceFile($path) {
    $result = @{ Part1 = @(); Part2 = @(); Found = $false }
    if (-not (Test-Path $path)) { return $result }
    $result.Found = $true
    $part = ''
    foreach ($line in [System.IO.File]::ReadAllLines($path)) {
        if ($line -match '^=== PART1') { $part = 'P1'; continue }
        if ($line -match '^=== PART2') { $part = 'P2'; continue }
        if ($line -match '^\s*#') { continue }
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $tok = $line.Trim() -split '\s+'
        if ($part -eq 'P1' -and $tok.Count -ge 19) {
            $result.Part1 += @{
                SL=$([int]$tok[0]); V_S=$tok[1]; V_E=$tok[2]; T_E=$tok[3]
                FX=$tok[4]; W=$tok[5]; CTL=$tok[6]; MSK=$tok[7]
                SSX=$tok[8]; SS_Y=$tok[9]; SSNT=$tok[10]
                NTw=$([int]$tok[11]); CYC=$tok[12]
                PC=$tok[13]; A=$tok[14]; X=$tok[15]; Y=$tok[16]; S=$tok[17]; P=$tok[18]
                PAL=$(if ($tok.Count -ge 20) { $tok[19] } else { '?' })
            }
        } elseif ($part -eq 'P2' -and $tok.Count -ge 7) {
            $result.Part2 += @{
                SEQ=$([int]$tok[0]); SL=$tok[1]; DOT=$tok[2]
                V_BEF=$tok[3]; V_AFT=$tok[4]; NTADDR=$tok[5]
                VAL=$tok[6]; PC=$(if ($tok.Count -ge 8) { $tok[7] } else { '?' })
            }
        }
    }
    return $result
}

Write-Host "Parsing traces..."
$nes1  = Parse-TraceFile $Nes1Trace
$fceux = Parse-TraceFile $FceuxTrace
Write-Host "  nes1:  $($nes1.Part1.Count) SLs, $($nes1.Part2.Count) writes"
Write-Host "  fceux: $($fceux.Part1.Count) SLs, $($fceux.Part2.Count) writes"

$nes1_p1  = @{}; foreach ($r in $nes1.Part1)  { $nes1_p1[$r.SL]  = $r }
$fceux_p1 = @{}; foreach ($r in $fceux.Part1) { $fceux_p1[$r.SL] = $r }

$sl_rows = @()
for ($sl = 0; $sl -lt 240; $sl++) {
    $n = $nes1_p1[$sl]; $f = $fceux_p1[$sl]
    $row = @{ SL=$sl; N=$n; F=$f }
    $row.VS_Match  = $n -and $f -and ($n.V_S  -eq $f.V_S)
    $row.VE_Match  = $n -and $f -and ($n.V_E  -eq $f.V_E)
    $row.TE_Match  = $n -and $f -and ($n.T_E  -eq $f.T_E)
    $row.FX_Match  = $n -and $f -and ($n.FX   -eq $f.FX)
    $row.W_Match   = $n -and $f -and ($n.W    -eq $f.W)
    $row.CTL_Match = $n -and $f -and ($n.CTL  -eq $f.CTL)
    $row.MSK_Match = $n -and $f -and ($n.MSK  -eq $f.MSK)
    $row.NTw_Match = $n -and $f -and ($n.NTw  -eq $f.NTw)
    $row.PC_Match  = $n -and $f -and ($n.PC   -eq $f.PC)
    $row.A_Match   = $n -and $f -and ($n.A    -eq $f.A)
    $row.X_Match   = $n -and $f -and ($n.X    -eq $f.X)
    $row.Y_Match   = $n -and $f -and ($n.Y    -eq $f.Y)
    $row.S_Match   = $n -and $f -and ($n.S    -eq $f.S)
    $row.P_Match   = $n -and $f -and ($n.P    -eq $f.P)
    $row.PAL_Match = $n -and $f -and ($n.PAL  -eq $f.PAL)
    $row.Any_Diff  = $n -and $f -and (-not (
        $row.VS_Match -and $row.VE_Match -and $row.TE_Match -and
        $row.FX_Match -and $row.W_Match  -and $row.CTL_Match -and $row.MSK_Match -and
        $row.NTw_Match -and $row.PC_Match -and $row.A_Match  -and $row.X_Match -and
        $row.Y_Match  -and $row.S_Match  -and $row.P_Match  -and $row.PAL_Match
    ))
    $sl_rows += $row
}

$max_seq = [Math]::Max($nes1.Part2.Count, $fceux.Part2.Count)
$w_rows  = @()
for ($i = 0; $i -lt $max_seq; $i++) {
    $n = if ($i -lt $nes1.Part2.Count)  { $nes1.Part2[$i]  } else { $null }
    $f = if ($i -lt $fceux.Part2.Count) { $fceux.Part2[$i] } else { $null }
    $row = @{ SEQ=($i+1); N=$n; F=$f }
    $row.SL_Match     = $n -and $f -and ($n.SL     -eq $f.SL)
    $row.NTADDR_Match = $n -and $f -and ($n.NTADDR -eq $f.NTADDR)
    $row.VAL_Match    = $n -and $f -and ($n.VAL    -eq $f.VAL)
    $row.Any_Diff     = $n -and $f -and (-not ($row.SL_Match -and $row.NTADDR_Match -and $row.VAL_Match))
    $row.Missing      = (-not $n) -or (-not $f)
    $w_rows += $row
}

$sl_diff_v   = ($sl_rows | Where-Object { $_.N -and $_.F -and (-not $_.VE_Match) }).Count
$sl_diff_ntw = ($sl_rows | Where-Object { $_.N -and $_.F -and (-not $_.NTw_Match) }).Count
$sl_diff_pal = ($sl_rows | Where-Object { $_.N -and $_.F -and (-not $_.PAL_Match) }).Count
$sl_diff_cpu = ($sl_rows | Where-Object { $_.N -and $_.F -and (-not ($_.PC_Match -and $_.A_Match -and $_.X_Match -and $_.Y_Match -and $_.S_Match -and $_.P_Match)) }).Count

Write-Host "Generating HTML report..."

function ce($s) { if ($null -eq $s) { return "" }; [System.Net.WebUtility]::HtmlEncode([string]$s) }
$sb = [System.Text.StringBuilder]::new()
$null = $sb.Append('<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><title>Frame 2 Trace Compare - Mappy</title>')
$null = $sb.Append('<style>*{box-sizing:border-box;margin:0;padding:0}body{font-family:Consolas,monospace;font-size:12px;background:#1a1b26;color:#c0caf5}')
$null = $sb.Append('.page-hdr{background:#24283b;padding:10px 18px;position:sticky;top:0;z-index:200;border-bottom:2px solid #414868;display:flex;align-items:center;gap:16px;flex-wrap:wrap}')
$null = $sb.Append('.page-hdr h1{font-size:15px;color:#bb9af7;white-space:nowrap}')
$null = $sb.Append('.badge{background:#2e3348;border-radius:4px;padding:2px 9px;font-size:11px;white-space:nowrap}.badge.ok{background:#1a2b1a;color:#9ece6a}.badge.bad{background:#2d1a1a;color:#f7768e}')
$null = $sb.Append('.controls{background:#1f2335;padding:6px 18px;border-bottom:1px solid #414868;display:flex;gap:18px;align-items:center;position:sticky;top:41px;z-index:199}')
$null = $sb.Append('.controls label{cursor:pointer;color:#a9b1d6;font-size:11px}input[type=checkbox]{accent-color:#bb9af7;margin-right:4px;cursor:pointer}')
$null = $sb.Append('.section{padding:14px 18px 6px}.section-title{font-size:13px;color:#7dcfff;padding-bottom:5px;border-bottom:1px solid #414868;margin-bottom:8px}')
$null = $sb.Append('.wrap{overflow-x:auto;margin-bottom:20px}table{border-collapse:collapse;width:100%;font-size:11px}')
$null = $sb.Append('th{background:#24283b;color:#a9b1d6;padding:3px 6px;text-align:left;white-space:nowrap;border-bottom:2px solid #414868}th.sl-col{background:#1f2335}')
$null = $sb.Append('th.nes-h{border-top:3px solid #7aa2f7;background:#1e2535}th.fcx-h{border-top:3px solid #ff9e64;background:#251e2a}th.cmp-h{border-top:3px solid #565f89;background:#1f2335;color:#565f89}')
$null = $sb.Append('td{padding:2px 6px;border-bottom:1px solid #1e2030;white-space:nowrap;color:#c0caf5}tr:hover td{background:#292e42}td.na{color:#3b3f5c}')
$null = $sb.Append('td.ind{text-align:center;font-size:11px;width:16px}td.ind.ok{color:#9ece6a}td.ind.bad{color:#f7768e;font-weight:bold}td.ind.na{color:#3b3f5c}')
$null = $sb.Append('td.diff{color:#f7768e;font-weight:bold}tr.row-bad{background:#1f1826}tr.row-hidden{display:none}')
$null = $sb.Append('.sep-nes{border-left:2px solid #7aa2f7}.sep-fcx{border-left:2px solid #ff9e64}.sep-cmp{border-left:1px solid #414868}')
$null = $sb.Append('.pal-box{font-family:Consolas,monospace;font-size:10px}</style></head><body>')
$badVE  = if ($sl_diff_v   -gt 0) { "bad" } else { "ok" }
$badNTW = if ($sl_diff_ntw -gt 0) { "bad" } else { "ok" }
$badPAL = if ($sl_diff_pal -gt 0) { "bad" } else { "ok" }
$badCPU = if ($sl_diff_cpu -gt 0) { "bad" } else { "ok" }
$null = $sb.Append("<div class='page-hdr'><h1>Frame 2 Trace Compare &mdash; Mappy (Japan)</h1>")
$null = $sb.Append("<span class='badge'>nes1 P2: $($nes1.Part2.Count)</span><span class='badge'>fceux P2: $($fceux.Part2.Count)</span>")
$null = $sb.Append("<span class='badge $badVE'>V_E diffs: $sl_diff_v/240</span><span class='badge $badNTW'>NTw diffs: $sl_diff_ntw/240</span>")
$null = $sb.Append("<span class='badge $badPAL'>PAL diffs: $sl_diff_pal/240</span><span class='badge $badCPU'>CPU diffs: $sl_diff_cpu/240</span></div>")
$null = $sb.Append("<div class='controls'><label><input type='checkbox' id='chk1' onchange='filterP1()'> Part 1 diffs only</label>")
$null = $sb.Append("<label><input type='checkbox' id='chk2' onchange='filterP2()'> Part 2 diffs only</label></div>")
# PART 1 table
$null = $sb.Append("<div class='section'><div class='section-title'>Part 1 &mdash; Per-Scanline (240 rows)</div><div class='wrap'><table id='tbl1'><thead>")
$null = $sb.Append("<tr><th rowspan='2' class='sl-col'>SL</th>")
$null = $sb.Append("<th colspan='8' class='nes-h sep-nes'>nes1</th>")
$null = $sb.Append("<th colspan='8' class='fcx-h sep-fcx'>FCEUX</th>")
$null = $sb.Append("<th colspan='15' class='cmp-h sep-cmp'>match? (= ok, &lt;&gt; diff)</th></tr>")
$null = $sb.Append("<tr><th class='nes-h sep-nes'>V_S</th><th class='nes-h'>V_E</th><th class='nes-h'>T_E</th><th class='nes-h'>CTL</th><th class='nes-h'>MSK</th><th class='nes-h'>NTw</th><th class='nes-h'>CYC</th><th class='nes-h'>PC</th>")
$null = $sb.Append("<th class='fcx-h sep-fcx'>V_S</th><th class='fcx-h'>V_E</th><th class='fcx-h'>T_E</th><th class='fcx-h'>CTL</th><th class='fcx-h'>MSK</th><th class='fcx-h'>NTw</th><th class='fcx-h'>CYC</th><th class='fcx-h'>PC</th>")
$null = $sb.Append("<th class='cmp-h sep-cmp'>V_S</th><th class='cmp-h'>V_E</th><th class='cmp-h'>T_E</th><th class='cmp-h'>CTL</th><th class='cmp-h'>MSK</th><th class='cmp-h'>NTw</th><th class='cmp-h'>PC</th>")
$null = $sb.Append("<th class='cmp-h'>A</th><th class='cmp-h'>X</th><th class='cmp-h'>Y</th><th class='cmp-h'>S</th><th class='cmp-h'>P</th><th class='cmp-h'>FX</th><th class='cmp-h'>W</th><th class='cmp-h'>PAL</th></tr></thead><tbody>")
$first_diff_p1 = -1
foreach ($row in $sl_rows) {
    $n = $row.N; $f = $row.F
    $hasN = $null -ne $n; $hasF = $null -ne $f
    $isDiff = $row.Any_Diff
    $trCls = if ($isDiff) { "row-bad" } else { "row-ok" }
    $dd = if ($isDiff) { " data-diff='1'" } else { "" }
    $null = $sb.Append("<tr class='$trCls'$dd><td>$($row.SL)</td>")
    $cols = @('V_S','V_E','T_E','CTL','MSK','NTw','CYC','PC')
    $first = $true
    foreach ($col in $cols) {
        $nv = if ($hasN) { $n.$col } else { $null }
        $fv = if ($hasF) { $f.$col } else { $null }
        $bad = $hasN -and $hasF -and ($nv -ne $fv)
        $cls = if ($bad) { 'diff' } else { 'val' }
        $sep = if ($first) { ' sep-nes' } else { '' }; $first = $false
        if ($hasN) { $null = $sb.Append("<td class='$cls$sep'>$(ce $nv)</td>") } else { $null = $sb.Append("<td class='na$sep'>-</td>") }
    }
    $first = $true
    foreach ($col in $cols) {
        $nv = if ($hasN) { $n.$col } else { $null }
        $fv = if ($hasF) { $f.$col } else { $null }
        $bad = $hasN -and $hasF -and ($nv -ne $fv)
        $cls = if ($bad) { 'diff' } else { 'val' }
        $sep = if ($first) { ' sep-fcx' } else { '' }; $first = $false
        if ($hasF) { $null = $sb.Append("<td class='$cls$sep'>$(ce $fv)</td>") } else { $null = $sb.Append("<td class='na$sep'>-</td>") }
    }
    $matchCols = @('VS_Match','VE_Match','TE_Match','CTL_Match','MSK_Match','NTw_Match','PC_Match','A_Match','X_Match','Y_Match','S_Match','P_Match','FX_Match','W_Match','PAL_Match')
    $first = $true
    foreach ($mc in $matchCols) {
        $sep = if ($first) { ' sep-cmp' } else { '' }; $first = $false
        if (-not $hasN -or -not $hasF) { $null = $sb.Append("<td class='ind na$sep'>-</td>") }
        elseif ($row.$mc) { $null = $sb.Append("<td class='ind ok$sep'>=</td>") }
        else { $null = $sb.Append("<td class='ind bad$sep'>&lt;&gt;</td>") }
    }
    $null = $sb.Append("</tr>")
    if ($first_diff_p1 -lt 0 -and $isDiff) { $first_diff_p1 = $row.SL }
}
$null = $sb.Append("</tbody></table></div>")
if ($first_diff_p1 -ge 0) { $null = $sb.Append("<p style='color:#f7768e;font-size:11px;margin-bottom:6px'>First difference at scanline $first_diff_p1</p>") }
# Palette diff detail
$palDiffSLs = @($sl_rows | Where-Object { $_.N -and $_.F -and (-not $_.PAL_Match) })
if ($palDiffSLs.Count -gt 0) {
    $null = $sb.Append("<div class='section-title' style='color:#ff9e64;margin-top:10px'>Palette Differences ($($palDiffSLs.Count) scanlines)</div>")
    $null = $sb.Append("<div class='wrap'><table><thead><tr><th>SL</th><th>Side</th>")
    for ($b = 0; $b -lt 32; $b++) { $null = $sb.Append("<th>$b</th>") }
    $null = $sb.Append("</tr></thead><tbody>")
    foreach ($row in $palDiffSLs) {
        $np = $row.N.PAL; $fp = $row.F.PAL
        if ($np.Length -lt 64 -or $fp.Length -lt 64) { continue }
        $null = $sb.Append("<tr class='row-bad'><td rowspan='2'>$($row.SL)</td><td class='sep-nes'>nes1</td>")
        for ($b = 0; $b -lt 32; $b++) {
            $nb = $np.Substring($b*2,2); $fb = $fp.Substring($b*2,2)
            $cls = if ($nb -ne $fb) { "diff" } else { "val" }
            $null = $sb.Append("<td class='pal-box $cls'>$nb</td>")
        }
        $null = $sb.Append("</tr>")
        $null = $sb.Append("<tr class='row-bad'><td class='sep-fcx'>fceux</td>")
        for ($b = 0; $b -lt 32; $b++) {
            $nb = $np.Substring($b*2,2); $fb = $fp.Substring($b*2,2)
            $cls = if ($nb -ne $fb) { "diff" } else { "val" }
            $null = $sb.Append("<td class='pal-box $cls'>$fb</td>")
        }
        $null = $sb.Append("</tr>")
    }
    $null = $sb.Append("</tbody></table></div>")
}
$null = $sb.Append("</div>")
# PART 2 table
$null = $sb.Append("<div class='section'><div class='section-title'>Part 2 &mdash; NT Write Log (nes1: $($nes1.Part2.Count) | fceux: $($fceux.Part2.Count))</div><div class='wrap'><table id='tbl2'><thead>")
$null = $sb.Append("<tr><th>SEQ</th><th class='nes-h sep-nes'>SL</th><th class='nes-h'>NTADDR</th><th class='nes-h'>VAL</th><th class='nes-h'>V_BEF</th><th class='nes-h'>V_AFT</th><th class='nes-h'>PC</th>")
$null = $sb.Append("<th class='fcx-h sep-fcx'>SL</th><th class='fcx-h'>NTADDR</th><th class='fcx-h'>VAL</th><th class='fcx-h'>V_BEF</th><th class='fcx-h'>V_AFT</th><th class='fcx-h'>PC</th>")
$null = $sb.Append("<th class='cmp-h sep-cmp'>SL</th><th class='cmp-h'>NTADDR</th><th class='cmp-h'>VAL</th></tr></thead><tbody>")
$first_diff_p2 = -1
foreach ($row in $w_rows) {
    $n = $row.N; $f = $row.F
    $hasN = $null -ne $n; $hasF = $null -ne $f
    $isDiff = $row.Any_Diff -or $row.Missing
    $trCls = if ($row.Missing) { "row-na" } elseif ($isDiff) { "row-bad" } else { "row-ok" }
    $dd = if ($isDiff) { " data-diff='1'" } else { "" }
    $null = $sb.Append("<tr class='$trCls'$dd><td>$($row.SEQ)</td>")
    $p2cols = @('SL','NTADDR','VAL','V_BEF','V_AFT','PC')
    $first = $true
    foreach ($col in $p2cols) {
        $nv = if ($hasN) { $n.$col } else { $null }
        $fv = if ($hasF) { $f.$col } else { $null }
        $bad = $hasN -and $hasF -and ($nv -ne $fv)
        $cls = if ($bad) { 'diff' } else { 'val' }
        $sep = if ($first) { ' sep-nes' } else { '' }; $first = $false
        if ($hasN) { $null = $sb.Append("<td class='$cls$sep'>$(ce $nv)</td>") } else { $null = $sb.Append("<td class='na$sep'>-</td>") }
    }
    $first = $true
    foreach ($col in $p2cols) {
        $nv = if ($hasN) { $n.$col } else { $null }
        $fv = if ($hasF) { $f.$col } else { $null }
        $bad = $hasN -and $hasF -and ($nv -ne $fv)
        $cls = if ($bad) { 'diff' } else { 'val' }
        $sep = if ($first) { ' sep-fcx' } else { '' }; $first = $false
        if ($hasF) { $null = $sb.Append("<td class='$cls$sep'>$(ce $fv)</td>") } else { $null = $sb.Append("<td class='na$sep'>-</td>") }
    }
    $mcs = @('SL_Match','NTADDR_Match','VAL_Match')
    $first = $true
    foreach ($mc in $mcs) {
        $sep = if ($first) { ' sep-cmp' } else { '' }; $first = $false
        if ($row.Missing) { $null = $sb.Append("<td class='ind na$sep'>-</td>") }
        elseif ($row.$mc) { $null = $sb.Append("<td class='ind ok$sep'>=</td>") }
        else { $null = $sb.Append("<td class='ind bad$sep'>&lt;&gt;</td>") }
    }
    $null = $sb.Append("</tr>")
    if ($first_diff_p2 -lt 0 -and $isDiff) { $first_diff_p2 = $row.SEQ }
}
$null = $sb.Append("</tbody></table></div></div>")
$null = $sb.Append('<script>function filterP1(){ var s=document.getElementById("chk1").checked; document.querySelectorAll("#tbl1 tbody tr").forEach(function(r){ r.classList.toggle("row-hidden",s&&!r.dataset.diff); }); }')
$null = $sb.Append('function filterP2(){ var s=document.getElementById("chk2").checked; document.querySelectorAll("#tbl2 tbody tr").forEach(function(r){ r.classList.toggle("row-hidden",s&&!r.dataset.diff); }); }</script></body></html>')
[System.IO.File]::WriteAllText($OutHtml, $sb.ToString(), [System.Text.Encoding]::UTF8)
Write-Host "HTML written to $OutHtml"
