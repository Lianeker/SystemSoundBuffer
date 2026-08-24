$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot

# Material realista para un buffer de WhatsApp: la primera mitad con audio,
# la segunda en silencio. Es el perfil que de verdad va a tener el buffer.
$wav = 'C:\Windows\Media\Alarm01.wav'
$player = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command',
    "`$p = New-Object Media.SoundPlayer '$wav'; 1..8 | ForEach-Object { `$p.PlaySync() }"
)
Start-Sleep -Milliseconds 800

$job = Start-Job -ScriptBlock {
    param($dir, $pid2)
    Start-Sleep -Seconds 6
    Stop-Process -Id $pid2 -Force -ErrorAction SilentlyContinue
} -ArgumentList $PSScriptRoot, $player.Id

Write-Output '=== capturando 12 s: ~6 con audio, ~6 en silencio ==='
& .\squeeze.exe rec 12 material.raw
Receive-Job $job -ErrorAction SilentlyContinue | Out-Null
Remove-Job $job -Force -ErrorAction SilentlyContinue
Stop-Process -Id $player.Id -Force -ErrorAction SilentlyContinue

Write-Output ''
Write-Output '=== midiendo compresion sobre ese material real ==='
& .\squeeze.exe test material.raw
