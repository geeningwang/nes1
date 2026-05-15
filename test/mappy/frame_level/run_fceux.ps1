$p = Start-Process -FilePath 'C:\Work\fceux-2.6.6\vc\x64\Release\fceux64.exe' `
    -ArgumentList @('-lua', 'C:\Work\nes1\test\mappy_f4_trace.lua', 'C:\Work\nes1\test\mappy_japan.nes') `
    -PassThru
Write-Host ('PID: ' + $p.Id)
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep 1
    if ($p.HasExited) { Write-Host ('Exited: ' + $p.ExitCode); break }
    Write-Host ('Waiting... ' + $i)
}
if (!$p.HasExited) { $p.Kill(); Write-Host 'Killed after timeout' }
Write-Host ('File exists: ' + (Test-Path 'C:\Work\nes1\test\mappy_out\fceux_sl_f4.txt'))
