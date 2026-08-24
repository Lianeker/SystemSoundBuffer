# ¿Depende la captura del volumen de salida del sistema?
#
# Hace falta una senal de amplitud CONSTANTE y conocida: con musica el nivel
# cambia solo y la comparacion no dice nada. Se genera un tono de 440 Hz a
# -6 dBFS y se reproduce en bucle continuo.
#
# CUIDADO: volumen.exe cambia el volumen maestro y lo restaura al terminar.
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')

$tono = Join-Path (Get-Location) 'tono-const.wav'
if (-not (Test-Path $tono)) {
    $rate = 48000; $secs = 10; $n = $rate * $secs
    $amp = 16384          # -6 dBFS exactos
    $data = New-Object byte[] ($n * 4)
    for ($i = 0; $i -lt $n; $i++) {
        $v = [int]([math]::Sin(2 * [math]::PI * 440 * $i / $rate) * $amp)
        $b = [BitConverter]::GetBytes([int16]$v)
        $data[$i*4] = $b[0]; $data[$i*4+1] = $b[1]
        $data[$i*4+2] = $b[0]; $data[$i*4+3] = $b[1]
    }
    $fs = [System.IO.File]::Create($tono)
    $bw = New-Object System.IO.BinaryWriter($fs)
    $bw.Write([char[]]'RIFF'); $bw.Write([int](36 + $data.Length))
    $bw.Write([char[]]'WAVE'); $bw.Write([char[]]'fmt ')
    $bw.Write([int]16); $bw.Write([int16]1); $bw.Write([int16]2)
    $bw.Write([int]$rate); $bw.Write([int]($rate * 4)); $bw.Write([int16]4); $bw.Write([int16]16)
    $bw.Write([char[]]'data'); $bw.Write([int]$data.Length); $bw.Write($data)
    $bw.Close(); $fs.Close()
    Write-Output "generado $tono (440 Hz a -6 dBFS, bucle exacto)"
}

# 440 Hz * 10 s = 4400 ciclos enteros, asi que PlayLooping empalma sin salto.
$cmd = "`$s = New-Object Media.SoundPlayer '$tono'; `$s.PlayLooping(); Start-Sleep -Seconds 120"
$p = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @('-NoProfile','-Command',$cmd)
Start-Sleep -Seconds 3
Write-Output "reproduciendo el tono en el proceso $($p.Id)"
Write-Output ''

.\volumen.exe $p.Id

Stop-Process -Id $p.Id -Force -EA SilentlyContinue
