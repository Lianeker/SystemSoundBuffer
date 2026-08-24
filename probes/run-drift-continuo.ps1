# Discrimina entre dos explicaciones del deficit del loopback por proceso:
#   (a) desajuste de frecuencia real  -> el deficit crece tambien con UN solo flujo continuo
#   (b) perdida en cada arranque/parada -> con un flujo continuo el deficit desaparece
# Para eso el emisor reproduce UN wav largo UNA sola vez, sin reabrir el flujo.
param([double]$Secs = 26)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build')

# Un WAV de 40 s generado aqui: 440 Hz, 48 kHz, estereo, 16 bits.
$path = Join-Path (Get-Location) 'tono40s.wav'
if (-not (Test-Path $path)) {
    $rate = 48000; $secsWav = 40; $n = $rate * $secsWav
    $data = New-Object byte[] ($n * 4)
    for ($i = 0; $i -lt $n; $i++) {
        $v = [int]([math]::Sin(2 * [math]::PI * 440 * $i / $rate) * 8000)
        $b = [BitConverter]::GetBytes([int16]$v)
        $data[$i*4] = $b[0]; $data[$i*4+1] = $b[1]
        $data[$i*4+2] = $b[0]; $data[$i*4+3] = $b[1]
    }
    $fs = [System.IO.File]::Create($path)
    $bw = New-Object System.IO.BinaryWriter($fs)
    $bw.Write([char[]]'RIFF'); $bw.Write([int](36 + $data.Length))
    $bw.Write([char[]]'WAVE'); $bw.Write([char[]]'fmt ')
    $bw.Write([int]16); $bw.Write([int16]1); $bw.Write([int16]2)
    $bw.Write([int]$rate); $bw.Write([int]($rate * 4)); $bw.Write([int16]4); $bw.Write([int16]16)
    $bw.Write([char[]]'data'); $bw.Write([int]$data.Length); $bw.Write($data)
    $bw.Close(); $fs.Close()
    Write-Output "generado $path (40 s de tono continuo)"
}

# UNA sola reproduccion: el flujo de render se abre una vez y no se cierra.
$player = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command',
    "`$p = New-Object Media.SoundPlayer '$path'; `$p.PlaySync()"
)
Write-Output "emisor de flujo continuo: PID $($player.Id)"
Start-Sleep -Milliseconds 1500

& .\ssb.exe drift --secs $Secs --csv drift-continuo.csv `
    --src output `
    --src "app:$($player.Id)"

Stop-Process -Id $player.Id -Force -ErrorAction SilentlyContinue
