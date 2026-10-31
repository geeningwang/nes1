<#
.SYNOPSIS
  Parity suite: runs nes1 and FCEUX on the repo ROMs and reports the first divergence of each.

.EXAMPLE
  powershell -File tool\run_parity.ps1                       # 3000 frames, frame-level digests
  powershell -File tool\run_parity.ps1 -Level scanline       # also every scanline of every frame
  powershell -File tool\run_parity.ps1 -Frames 300 -Only mappy
  powershell -File tool\run_parity.ps1 -TraceFrames 1-8,100,3000       # full instruction traces of chosen frames
  powershell -File tool\run_parity.ps1 -Frames 10 -RomDir C:\work\sweep   # see tool\make_opcode_sweep.py

  Each ROM is run through tool\find_divergence.py, which drills down from the first differing frame
  to the scanline, the instruction and the differing fields.  Output goes to <Work>\<name>\.
  The exit code is 0 only if every ROM is identical (see doc/parity_plan.md, section 1).
#>
param(
    [int]$Frames = 3000,
    [ValidateSet('frame', 'scanline')][string]$Level = 'frame',
    [string]$Only = '',
    [string]$Work = 'C:\work\parity',
    [int]$TimeoutSec = 600,
    [string]$Nes1,
    [string]$Fceux,
    [string]$Python,
    [string]$TraceFrames = '',   # compare full instruction traces of these frames (e.g. '1-8,100,3000') instead of the digest search
    [string]$RomDir = ''     # run every ROM of this directory instead of the standard suite (e.g. the opcode sweep)
)

$repo = Split-Path -Parent $PSScriptRoot
if (-not $Nes1)   { $Nes1   = Join-Path $repo 'src\x64\Release\nes1.exe' }
if (-not $Fceux)  { $Fceux  = Join-Path $repo 'tool\fceux-2.6.6\vc\x64\Release\fceux64.exe' }
if (-not $Python) {
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    $Python = if ($cmd) { $cmd.Source } else { 'C:\work\python\python.exe' }
}
foreach ($f in @($Nes1, $Fceux, $Python)) {
    if (-not (Test-Path $f)) { Write-Host "ERROR: not found: $f"; exit 2 }
}

# name, ROM (relative to the repo), extra find_divergence.py arguments
$suite = @(
    @('nestest_auto', 'test\nestest.nes',               @('--init-pc', 'C000')),
    @('nestest',      'test\nestest.nes',               @()),
    @('mappy',        'test\mappy\Mappy (Japan).nes',   @()),
    @('color_test',   'test\ppu\color_test.nes',        @())
)
$probeDir = Join-Path $repo 'test\probe'
if (Test-Path $probeDir) {
    Get-ChildItem $probeDir -Filter *.nes | ForEach-Object {
        $suite += ,@(('probe_' + $_.BaseName), ('test\probe\' + $_.Name), @())
    }
}

if ($RomDir) {
    $suite = @()
    Get-ChildItem $RomDir -Filter *.nes | ForEach-Object { $suite += ,@($_.BaseName, $_.FullName, @()) }
}

$results = @()
foreach ($t in $suite) {
    if ($Only -and $t[0] -notlike "*$Only*") { continue }
    $rom = if ($RomDir) { $t[1] } else { Join-Path $repo $t[1] }
    $out = Join-Path $Work $t[0]
    Write-Host "=== $($t[0]) ($Frames frames, $Level level)"
    if ($TraceFrames -ne '') {
        $a = @((Join-Path $PSScriptRoot 'compare_traces.py'), '--nes1', $Nes1, '--fceux', $Fceux, '--rom', $rom,
               '--frames', $Frames, '--work', $out, '--trace-frames', $TraceFrames, '--timeout', $TimeoutSec) + $t[2]
    } else {
    $a = @((Join-Path $PSScriptRoot 'find_divergence.py'), '--nes1', $Nes1, '--fceux', $Fceux, '--rom', $rom,
           '--frames', $Frames, '--work', $out, '--level', $Level, '--timeout', $TimeoutSec) + $t[2]
    }
    $log = & $Python @a 2>&1
    $code = $LASTEXITCODE
    $log | ForEach-Object { Write-Host "    $_" }
    Set-Content -Path ($out + '.txt') -Value $log -ErrorAction SilentlyContinue
    $results += [pscustomobject]@{ Name = $t[0]; Code = $code }
}

Write-Host ''
Write-Host '=== summary'
$bad = 0
foreach ($r in $results) {
    $state = switch ($r.Code) { 0 { 'IDENTICAL' } 1 { 'DIFFERS' } default { 'ERROR (run failed or hung)' } }
    Write-Host ("  {0,-20} {1}" -f $r.Name, $state)
    if ($r.Code -ne 0) { $bad++ }
}
if ($bad -eq 0) { Write-Host 'PARITY OK' } else { Write-Host "PARITY FAILED: $bad of $($results.Count) differ" }
exit $(if ($bad -eq 0) { 0 } else { 1 })
