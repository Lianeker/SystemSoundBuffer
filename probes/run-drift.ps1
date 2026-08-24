# Mide la serie de sincronizacion de los tres tipos de fuente a la vez, con una
# app emitiendo audio de verdad (si el loopback por proceso no recibe nada, el
# desfase no se puede caracterizar).
param([double]$Secs = 25, [string]$Csv = 'drift.csv')
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build')

$wav = 'C:\Windows\Media\Alarm01.wav'
$player = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command',
    "`$p = New-Object Media.SoundPlayer '$wav'; 1..40 | ForEach-Object { `$p.PlaySync() }"
)
Write-Output "emisor: PID $($player.Id)"
Start-Sleep -Milliseconds 1200

& .\ssb.exe drift --secs $Secs --csv $Csv `
    --src output `
    --src "app:$($player.Id)" `
    --src input

Stop-Process -Id $player.Id -Force -ErrorAction SilentlyContinue
