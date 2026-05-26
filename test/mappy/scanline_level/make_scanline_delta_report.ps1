param(
    [string]$Nes1Dir  = "C:\Work\nes1\test\mappy_out\nes1_sl_dump_f3",
    [string]$FceuxDir = "C:\Work\nes1\test\mappy_out\fceux_sl_dump_f3",
    [string]$OutHtml  = "C:\Work\nes1\test\mappy_out\f3_delta_report.html",
    [int]$Frame       = 3
)

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

function Parse-ScanlineState([string]$path) {
    $state = [ordered]@{
        Exists = $false
        CpuLine = ''
        PC = ''
        A = ''
        X = ''
        Y = ''
        S = ''
        P = ''
        Cycles = $null
        PPUCTRL = ''
        PPUMASK = ''
        PPUSTATUS = ''
        V = ''
        T = ''
        FineX = ''
        ScrollX = ''
        ScrollY = ''
        ScrollNT = ''
        RenderScrollLine = ''
        NtWriteRows = @()
        NtWriteSummary = ''
    }
    if (-not (Test-Path $path)) { return [pscustomobject]$state }

    $state.Exists = $true
    $section = ''
    foreach ($line in [System.IO.File]::ReadAllLines($path)) {
        if ($line -match '^=== ') {
            if ($line -match '^=== NT Writes During Scanline ===') { $section = 'NTWRITES' }
            else { $section = '' }
            continue
        }
        if ($line -match '^PC=([0-9A-F]+) A=([0-9A-F]+) X=([0-9A-F]+) Y=([0-9A-F]+) S=([0-9A-F]+) P=([0-9A-F]+) cycles=(\d+)') {
            $state.CpuLine = $line.Trim()
            $state.PC = $Matches[1]
            $state.A = $Matches[2]
            $state.X = $Matches[3]
            $state.Y = $Matches[4]
            $state.S = $Matches[5]
            $state.P = $Matches[6]
            $state.Cycles = [long]$Matches[7]
        } elseif ($line -match '^PPUCTRL\s+\(\$2000\):\s+([0-9A-F]{2})') {
            $state.PPUCTRL = $Matches[1]
        } elseif ($line -match '^PPUMASK\s+\(\$2001\):\s+([0-9A-F]{2})') {
            $state.PPUMASK = $Matches[1]
        } elseif ($line -match '^PPUSTATUS\(\$2002\):\s+([0-9A-F]{2})') {
            $state.PPUSTATUS = $Matches[1]
        } elseif ($line -match '^v=([0-9A-F]{4})\s+t=([0-9A-F]{4})\s+fine_x=(\d+)') {
            $state.V = $Matches[1]
            $state.T = $Matches[2]
            $state.FineX = $Matches[3]
        } elseif ($line -match '^render_scroll_x=(\d+).+render_scroll_y=(\d+).+render_scroll_nt=(\d+)') {
            $state.ScrollX = $Matches[1]
            $state.ScrollY = $Matches[2]
            $state.ScrollNT = $Matches[3]
            $state.RenderScrollLine = $line.Trim()
        } elseif ($section -eq 'NTWRITES') {
            if ($line.Trim() -eq '' -or $line.Trim() -eq '(none)') { continue }
            if ($line -match '^\s*#') { continue }
            if ($line -match '^\s*(\d+)\s+(\d+)\s+([0-9A-F]{4})\s+([0-9A-F]{2})\s+([0-9A-F]{4})\s*$') {
                $state.NtWriteRows += ('{0}:{1}:{2}:{3}:{4}' -f $Matches[1], $Matches[2], $Matches[3], $Matches[4], $Matches[5])
            }
        }
    }
    $state.NtWriteSummary = ($state.NtWriteRows -join '; ')

    return [pscustomobject]$state
}

function HE([object]$s) {
    if ($null -eq $s) { return '' }
    return [System.Net.WebUtility]::HtmlEncode([string]$s)
}

function Add-Diff([System.Collections.Generic.List[string]]$diffs, [string]$label, [object]$left, [object]$right) {
    $ls = if ($null -eq $left) { '' } else { [string]$left }
    $rs = if ($null -eq $right) { '' } else { [string]$right }
    if ($ls -ne $rs) { $diffs.Add($label) }
}

function Add-FieldSummary($map, [string]$label, [int]$scanline) {
    if (-not $map.Contains($label)) {
        $map[$label] = [ordered]@{ Count = 0; First = $scanline }
    }
    $map[$label].Count++
}

function Format-RangeList([int[]]$values) {
    if (-not $values -or $values.Count -eq 0) { return 'none' }
    $parts = [System.Collections.Generic.List[string]]::new()
    $start = $values[0]
    $prev = $values[0]
    for ($i = 1; $i -lt $values.Count; ++$i) {
        $v = $values[$i]
        if ($v -eq ($prev + 1)) {
            $prev = $v
            continue
        }
        if ($start -eq $prev) { $parts.Add([string]$start) }
        else { $parts.Add(('{0}-{1}' -f $start, $prev)) }
        $start = $v
        $prev = $v
    }
    if ($start -eq $prev) { $parts.Add([string]$start) }
    else { $parts.Add(('{0}-{1}' -f $start, $prev)) }
    return ($parts -join ', ')
}

$nes1Status = Get-DumpStatus $Nes1Dir
$fceuxStatus = Get-DumpStatus $FceuxDir
if ($nes1Status.Status -ne 'valid' -or $fceuxStatus.Status -ne 'valid') {
    throw "Scanline dumps are not valid for frame $Frame. NES1=$($nes1Status.Status), FCEUX=$($fceuxStatus.Status)"
}

$rows = [System.Collections.Generic.List[object]]::new()
$fieldStats = @{}
$exactStructured = [System.Collections.Generic.List[int]]::new()
$exactText = [System.Collections.Generic.List[int]]::new()
$firstStructuredMismatch = $null

for ([int]$sl = 0; $sl -lt 240; ++$sl) {
    $nTxt = Join-Path $Nes1Dir  ("nes1_frame_{0:D4}_sl_{1:D3}.txt" -f $Frame, $sl)
    $fTxt = Join-Path $FceuxDir ("fceux_frame_{0:D4}_sl_{1:D3}.txt" -f $Frame, $sl)

    $nState = Parse-ScanlineState $nTxt
    $fState = Parse-ScanlineState $fTxt
    $nNorm = Get-NormalizedText $nTxt
    $fNorm = Get-NormalizedText $fTxt

    $diffs = [System.Collections.Generic.List[string]]::new()
    Add-Diff $diffs 'file' $nState.Exists $fState.Exists
    Add-Diff $diffs 'pc' $nState.PC $fState.PC
    Add-Diff $diffs 'a' $nState.A $fState.A
    Add-Diff $diffs 'x' $nState.X $fState.X
    Add-Diff $diffs 'y' $nState.Y $fState.Y
    Add-Diff $diffs 's' $nState.S $fState.S
    Add-Diff $diffs 'p' $nState.P $fState.P
    Add-Diff $diffs 'cycles' $nState.Cycles $fState.Cycles
    Add-Diff $diffs 'ppuctrl' $nState.PPUCTRL $fState.PPUCTRL
    Add-Diff $diffs 'ppumask' $nState.PPUMASK $fState.PPUMASK
    Add-Diff $diffs 'ppustatus' $nState.PPUSTATUS $fState.PPUSTATUS
    Add-Diff $diffs 'v' $nState.V $fState.V
    Add-Diff $diffs 't' $nState.T $fState.T
    Add-Diff $diffs 'fine_x' $nState.FineX $fState.FineX
    Add-Diff $diffs 'scroll_x' $nState.ScrollX $fState.ScrollX
    Add-Diff $diffs 'scroll_y' $nState.ScrollY $fState.ScrollY
    Add-Diff $diffs 'scroll_nt' $nState.ScrollNT $fState.ScrollNT
    Add-Diff $diffs 'nt_writes' $nState.NtWriteSummary $fState.NtWriteSummary

    $cycleDelta = $null
    if ($null -ne $nState.Cycles -and $null -ne $fState.Cycles) {
        $cycleDelta = [long]$nState.Cycles - [long]$fState.Cycles
    }

    $structuredOK = ($diffs.Count -eq 0)
    $rawTextOK = ($nNorm -eq $fNorm)
    if ($structuredOK) { $exactStructured.Add($sl) }
    if ($rawTextOK) { $exactText.Add($sl) }

    if (-not $structuredOK) {
        if ($null -eq $firstStructuredMismatch) { $firstStructuredMismatch = $sl }
        foreach ($label in $diffs) { Add-FieldSummary $fieldStats $label $sl }
    }

    $rows.Add([pscustomobject]@{
        Scanline = $sl
        StructuredOK = $structuredOK
        RawTextOK = $rawTextOK
        FirstDiff = if ($structuredOK) { '' } else { $diffs[0] }
        DiffList = ($diffs -join ', ')
        CycleDelta = $cycleDelta
        Nes1Cpu = $nState.CpuLine
        FceuxCpu = $fState.CpuLine
        Nes1VT = ('v={0} t={1} fine_x={2}' -f $nState.V, $nState.T, $nState.FineX)
        FceuxVT = ('v={0} t={1} fine_x={2}' -f $fState.V, $fState.T, $fState.FineX)
        Nes1Scroll = $nState.RenderScrollLine
        FceuxScroll = $fState.RenderScrollLine
        Nes1Regs = ('PPUCTRL={0} PPUMASK={1} PPUSTATUS={2}' -f $nState.PPUCTRL, $nState.PPUMASK, $nState.PPUSTATUS)
        FceuxRegs = ('PPUCTRL={0} PPUMASK={1} PPUSTATUS={2}' -f $fState.PPUCTRL, $fState.PPUMASK, $fState.PPUSTATUS)
        Nes1NtWrites = $nState.NtWriteSummary
        FceuxNtWrites = $fState.NtWriteSummary
    })
}

$summaryRows = [System.Text.StringBuilder]::new()
foreach ($entry in ($fieldStats.GetEnumerator() | Sort-Object { $_.Value.First }, Name)) {
    $null = $summaryRows.AppendLine(
        ('<tr><td>{0}</td><td>{1}</td><td>{2}</td></tr>' -f (HE $entry.Key), $entry.Value.Count, $entry.Value.First)
    )
}
if ($summaryRows.Length -eq 0) {
    $null = $summaryRows.AppendLine('<tr><td colspan="3">No structured diffs</td></tr>')
}

$tableRows = [System.Text.StringBuilder]::new()
foreach ($row in $rows) {
    $trClass = if ($row.StructuredOK) { 'okrow' } else { 'badrow' }
    $status = if ($row.StructuredOK) { '<span class="ok">match</span>' } else { '<span class="bad">diff</span>' }
    $delta = if ($null -eq $row.CycleDelta) { '' } elseif ($row.CycleDelta -eq 0) { '0' } else { [string]$row.CycleDelta }
    $detail = if ($row.StructuredOK) {
        '<span class="ok">exact</span>'
    } else {
        '<code class="d">{0}</code><br><code class="d2">{1}</code>' -f (HE $row.Nes1Cpu), (HE $row.FceuxCpu)
    }
    $ppuDetail = if ($row.StructuredOK) {
        '<code>{0}</code>' -f (HE $row.Nes1Regs)
    } else {
        '<code class="d">{0}</code><br><code class="d2">{1}</code>' -f (HE $row.Nes1Regs), (HE $row.FceuxRegs)
    }
    $loopyDetail = if ($row.StructuredOK) {
        '<code>{0}</code><br><code>{1}</code>' -f (HE $row.Nes1VT), (HE $row.Nes1Scroll)
    } else {
        '<code class="d">{0}</code><br><code class="d2">{1}</code><br><code class="d">{2}</code><br><code class="d2">{3}</code>' -f (HE $row.Nes1VT), (HE $row.FceuxVT), (HE $row.Nes1Scroll), (HE $row.FceuxScroll)
    }
    $ntWriteDetail = if ($row.Nes1NtWrites -or $row.FceuxNtWrites) {
        if ($row.Nes1NtWrites -eq $row.FceuxNtWrites) {
            '<br><code>{0}</code>' -f (HE $row.Nes1NtWrites)
        } else {
            '<br><code class="d">{0}</code><br><code class="d2">{1}</code>' -f (HE $row.Nes1NtWrites), (HE $row.FceuxNtWrites)
        }
    } else { '' }
    $rawCell = if ($row.RawTextOK) { '<span class="ok">same</span>' } else { '<span class="bad">diff</span>' }

    $null = $tableRows.AppendLine(
        ('<tr class="{0}" data-diff="{1}"><td>{2:D3}</td><td>{3}</td><td>{4}</td><td>{5}</td><td>{6}</td><td>{7}</td><td>{8}</td><td>{9}</td></tr>' -f
            $trClass,
            ($(if ($row.StructuredOK) { '0' } else { '1' })),
            $row.Scanline,
            $status,
            (HE $row.FirstDiff),
            (HE $row.DiffList),
            (HE $delta),
            $detail,
            $ppuDetail,
            ($loopyDetail + $ntWriteDetail + '<br>' + $rawCell))
    )
}

$now = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
$exactStructuredText = Format-RangeList ([int[]]$exactStructured)
$exactTextText = Format-RangeList ([int[]]$exactText)
$structuredDiffCount = 240 - $exactStructured.Count
$rawTextDiffCount = 240 - $exactText.Count
$badgeClass = if ($structuredDiffCount -eq 0) { 'ok' } else { 'bad' }
$badgeText = if ($structuredDiffCount -eq 0) { 'ALL STRUCTURED MATCH' } else { "$structuredDiffCount STRUCTURED DIFFS" }
$focusRow = if ($null -eq $firstStructuredMismatch) {
    '<div class="focus okbox">No structured mismatch found.</div>'
} else {
    $focus = $rows[$firstStructuredMismatch]
    @"
<div class="focus badbox">
  <div><b>Earliest structured mismatch:</b> scanline $($focus.Scanline)</div>
  <div><b>Fields:</b> $(HE $focus.DiffList)</div>
  <div><b>NES1:</b> <code>$(HE $focus.Nes1Cpu)</code></div>
  <div><b>FCEUX:</b> <code>$(HE $focus.FceuxCpu)</code></div>
  <div><b>Cycle delta:</b> $(HE $focus.CycleDelta)</div>
</div>
"@
}

$html = @"
<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Frame $Frame Scanline Delta Report</title>
<style>
body{font-family:Consolas,monospace;background:#111;color:#ddd;margin:0;padding:16px;font-size:13px}
h1{color:#f0c040;margin:0 0 6px}
.meta{color:#aaa;margin-bottom:12px}
.badge{display:inline-block;padding:4px 12px;border-radius:3px;color:#fff;font-weight:bold}
.badge.ok,.ok{color:#9ece6a}.badge.ok{background:#214621;color:#fff}
.badge.bad,.bad{color:#f7768e}.badge.bad{background:#652525;color:#fff}
.focus{margin:16px 0;padding:12px;border:1px solid #333}
.okbox{background:#112211;border-color:#214621}
.badbox{background:#2a1010;border-color:#652525}
.grid{display:grid;grid-template-columns:1.3fr 1fr;gap:16px;align-items:start}
table{border-collapse:collapse;width:100%}
th{position:sticky;top:0;background:#24283b;color:#f0c040;padding:5px 8px;text-align:left;z-index:2}
td{border:1px solid #2a2a2a;padding:4px 7px;vertical-align:top}
tr.okrow{background:#111e11}tr.badrow{background:#2a1010}
code{display:block;white-space:pre-wrap;word-break:break-word}
code.d{color:#ff8a65}
code.d2{color:#80cbc4}
.controls{margin:12px 0;color:#bbb}
.summary td,.summary th{font-size:12px}
</style></head>
<body>
<h1>Mappy Frame $Frame Scanline Delta Report</h1>
<div class="meta">Generated: $(HE $now) | NES1: $(HE $Nes1Dir) | FCEUX: $(HE $FceuxDir)</div>
<div><span class="badge $badgeClass">$badgeText</span> &nbsp; Structured matches: <b>$($exactStructured.Count)</b> / 240 &nbsp; Raw text matches: <b>$($exactText.Count)</b> / 240</div>
$focusRow
<div class="grid">
  <div>
    <table class="summary">
      <thead><tr><th>Field</th><th>Diff count</th><th>First scanline</th></tr></thead>
      <tbody>
      $summaryRows
      </tbody>
    </table>
  </div>
  <div>
    <table class="summary">
      <tbody>
        <tr><th>Structured exact scanlines</th><td>$(HE $exactStructuredText)</td></tr>
        <tr><th>Raw-text exact scanlines</th><td>$(HE $exactTextText)</td></tr>
        <tr><th>Structured diff count</th><td>$structuredDiffCount</td></tr>
        <tr><th>Raw-text diff count</th><td>$rawTextDiffCount</td></tr>
      </tbody>
    </table>
  </div>
</div>

<div class="controls"><label><input type="checkbox" id="diffOnly" onchange="toggleDiffs()" checked> show diffs only</label></div>
<table>
  <thead>
    <tr>
      <th>SL</th>
      <th>Status</th>
      <th>First diff</th>
      <th>All differing fields</th>
      <th>Cycle delta</th>
      <th>CPU</th>
      <th>PPU regs</th>
      <th>V/T, scroll, raw text</th>
    </tr>
  </thead>
  <tbody>
    $tableRows
  </tbody>
</table>

<script>
function toggleDiffs(){
  var only=document.getElementById('diffOnly').checked;
  document.querySelectorAll('tbody tr.badrow, tbody tr.okrow').forEach(function(row){
    row.style.display = (only && row.getAttribute('data-diff') === '0') ? 'none' : '';
  });
}
toggleDiffs();
</script>
</body></html>
"@

[System.IO.File]::WriteAllText($OutHtml, $html, [System.Text.Encoding]::UTF8)

Write-Host "Report written to: $OutHtml"
Write-Host "Structured matches: $($exactStructured.Count) / 240"
Write-Host "Raw text matches:   $($exactText.Count) / 240"
if ($null -ne $firstStructuredMismatch) {
    Write-Host "Earliest structured mismatch: scanline $firstStructuredMismatch"
    foreach ($entry in ($fieldStats.GetEnumerator() | Sort-Object { $_.Value.First }, Name)) {
        Write-Host ("  {0}: count={1} first={2}" -f $entry.Key, $entry.Value.Count, $entry.Value.First)
    }
}
