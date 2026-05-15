$p = Start-Process -FilePath 'C:\Work\fceux-2.6.6\vc\x64\Release\fceux64.exe' `
    -ArgumentList @('-lua', 'C:\Work\nes1\test\debug_test.lua', 'C:\Work\nes1\test\mappy_japan.nes') `
    -PassThru
Write-Host ('PID: ' + $p.Id)
for ($i = 0; $i -lt 20; $i++) {
    Start-Sleep 1
    if ($p.HasExited) { Write-Host ('Exited: ' + $p.ExitCode); break }
    Write-Host ('Waiting... ' + $i)
}
if (!$p.HasExited) { $p.Kill(); Write-Host 'Killed after timeout' }
$dbg = 'C:\Work\nes1\test\mappy_out\fceux_debug.txt'
Write-Host ('Debug file exists: ' + (Test-Path $dbg))
if (Test-Path $dbg) { Get-Content $dbg }
