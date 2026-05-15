param([int]$Frame = 244)
$fn = '{0:D4}' -f $Frame
$n = Get-Content "C:\Work\nes1\test\mappy_out\nes1_dense_out\nes1_frame_$fn.txt"
$f = Get-Content "C:\Work\nes1\test\mappy_out\fceux_dense_out\fceux_frame_$fn.txt"
for ($i = 0; $i -lt $n.Length; $i++) {
    if ($n[$i] -ne $f[$i]) {
        $nLine = $n[$i]
        $fLine = $f[$i]
        Write-Host "Line $i differs"
        Write-Host "NES1: '$nLine'"
        Write-Host "FCEU: '$fLine'"
        for ($c = 0; $c -lt [Math]::Max($nLine.Length, $fLine.Length); $c++) {
            $nc = if ($c -lt $nLine.Length) { $nLine[$c] } else { ' ' }
            $fc = if ($c -lt $fLine.Length) { $fLine[$c] } else { ' ' }
            if ($nc -ne $fc) {
                Write-Host ("  Col " + $c + ": NES1='" + $nc + "' FCEUX='" + $fc + "'")
            }
        }
    }
}
