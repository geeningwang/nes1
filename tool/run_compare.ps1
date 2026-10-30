<#
.SYNOPSIS
  Run nes1 and the patched FCEUX with identical --dump arguments, then compare the outputs.

.EXAMPLE
  powershell -File tool\run_compare.ps1 -Rom "test\mappy\Mappy (Japan).nes" -Tag mappy `
      -DumpArgs "--frames 120 --dump-frames 1-120 --dump-scanline-frames 5 --dump-pc C06F --pc-max 5"

  Output goes to <OutRoot>\<Tag>\nes1 and <OutRoot>\<Tag>\fceux; the comparison report is printed.
  The exit code is that of compare_dumps.py (0 identical, 1 differ, 2 error).

  -DumpArgs is split on spaces, so option values must not contain spaces. See doc/state_dump.md.
#>
param(
    [Parameter(Mandatory = $true)][string]$Rom,
    [Parameter(Mandatory = $true)][string]$DumpArgs,
    [string]$Tag = 'run',
    [string]$OutRoot = 'C:\work',
    [string]$Nes1,
    [string]$Fceux,
    [string]$Python,
    [int]$Max = 0
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Nes1)   { $Nes1   = Join-Path $repo 'src\x64\Debug\nes1.exe' }
if (-not $Fceux)  { $Fceux  = Join-Path $repo 'tool\fceux-2.6.6\vc\x64\Release\fceux64.exe' }
if (-not $Python) {
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    $Python = if ($cmd) { $cmd.Source } else { 'C:\work\python\python.exe' }
}
$Rom = (Resolve-Path $Rom).Path
foreach ($f in @($Nes1, $Fceux, $Python)) {
    if (-not (Test-Path $f)) { Write-Host "ERROR: not found: $f"; exit 2 }
}

$extra = $DumpArgs -split '\s+' | Where-Object { $_ }
$dirs = @{}
foreach ($e in @(@('nes1', $Nes1), @('fceux', $Fceux))) {
    $out = Join-Path (Join-Path $OutRoot $Tag) $e[0]
    if (Test-Path $out) { Remove-Item $out -Recurse -Force }
    $dirs[$e[0]] = $out
    $argList = @("`"$Rom`"", '--dump', "`"$out`"") + $extra
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process $e[1] -ArgumentList $argList -PassThru -Wait -NoNewWindow
    $n = (Get-ChildItem $out -File -ErrorAction SilentlyContinue | Measure-Object).Count
    Write-Host ("{0}: exit={1} time={2}s files={3}" -f $e[0], $p.ExitCode, [int]$sw.Elapsed.TotalSeconds, $n)
    if ($p.ExitCode -ne 0) { Write-Host "ERROR: $($e[0]) failed (exit $($p.ExitCode))"; exit 2 }
}

& $Python (Join-Path $PSScriptRoot 'compare_dumps.py') $dirs['nes1'] $dirs['fceux'] --summary --max $Max
exit $LASTEXITCODE
