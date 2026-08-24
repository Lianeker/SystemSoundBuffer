$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot

$wav = 'C:\Windows\Media\Alarm01.wav'
if (-not (Test-Path $wav)) { $wav = (Get-ChildItem 'C:\Windows\Media\*.wav' | Select-Object -First 1).FullName }
Write-Output "wav de prueba: $wav"

# Un proceso que emite audio de verdad durante ~20 s.
$player = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command',
    "`$p = New-Object Media.SoundPlayer '$wav'; 1..12 | ForEach-Object { `$p.PlaySync() }"
)
Write-Output "proceso emisor: PID $($player.Id)"
Start-Sleep -Milliseconds 1200

Write-Output ''
Write-Output '=== A. loopback del dispositivo de salida (todo el PC) ==='
& .\probe.exe loop 3

Write-Output ''
Write-Output "=== B. loopback SOLO del proceso emisor (PID $($player.Id), INCLUDE_TREE) ==="
& .\probe.exe proc $player.Id 3

Write-Output ''
Write-Output "=== C. loopback EXCLUYENDO al proceso emisor (debe salir silencio) ==="
& .\probe.exe excl $player.Id 3

Write-Output ''
$wa = Get-Process | Where-Object { $_.ProcessName -like '*Whats*' } | Select-Object -First 1
if ($wa) {
    Write-Output "=== D. loopback SOLO de WhatsApp ($($wa.ProcessName), PID $($wa.Id), INCLUDE_TREE) ==="
    & .\probe.exe proc $wa.Id 3
} else {
    Write-Output '=== D. WhatsApp no esta corriendo ==='
}

Stop-Process -Id $player.Id -Force -ErrorAction SilentlyContinue
Write-Output ''
Write-Output 'fin'
