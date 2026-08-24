# Prueba de extremo a extremo del motor: cuatro pistas a la vez, buffer circular
# mas corto que la grabacion (para que descarte de verdad) y volcado a WAV.
param([string]$Out = 'ssb-buffer', [double]$Secs = 14, [double]$Buffer = 8)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build')

$wav = 'C:\Windows\Media\Alarm01.wav'
$player = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command',
    "`$p = New-Object Media.SoundPlayer '$wav'; 1..60 | ForEach-Object { `$p.PlaySync() }"
)
Write-Output "emisor de prueba: PID $($player.Id)"
Start-Sleep -Milliseconds 1200

# Buffer de 8 s con 14 s de grabacion: el circular tiene que descartar en vivo.
& .\ssb.exe rec --secs $Secs --buffer $Buffer `
    --src output `
    --src app:WhatsApp `
    --src "app:$($player.Id)" `
    --src input `
    --out $Out

Stop-Process -Id $player.Id -Force -ErrorAction SilentlyContinue

Write-Output ''
Write-Output '=== WAV generados ==='
Get-ChildItem (Join-Path $Out '*.wav') | ForEach-Object {
    Write-Output ("  {0,-14} {1,10:N0} bytes" -f $_.Name, $_.Length)
}
Write-Output ''
Write-Output '=== segmentos que quedan en el buffer circular ==='
Get-ChildItem $Out -Directory | ForEach-Object {
    $segs = Get-ChildItem $_.FullName -Filter *.dat -ErrorAction SilentlyContinue
    $kb = ($segs | Measure-Object -Property Length -Sum).Sum / 1KB
    Write-Output ("  {0,-8} {1} segmento(s), {2:N0} KB" -f $_.Name, $segs.Count, $kb)
}
