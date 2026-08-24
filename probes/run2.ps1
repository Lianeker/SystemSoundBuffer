$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot

function Start-Player($wav) {
    Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
        '-NoProfile', '-Command',
        "`$p = New-Object Media.SoundPlayer '$wav'; 1..20 | ForEach-Object { `$p.PlaySync() }"
    )
}

$wavA = 'C:\Windows\Media\Alarm01.wav'
$wavB = 'C:\Windows\Media\Ring01.wav'
if (-not (Test-Path $wavB)) { $wavB = 'C:\Windows\Media\notify.wav' }
Write-Output "app A -> $wavA"
Write-Output "app B -> $wavB"

$a = Start-Player $wavA
$b = Start-Player $wavB
Write-Output "PID A = $($a.Id)   PID B = $($b.Id)"
Start-Sleep -Milliseconds 1500

Write-Output ''
Write-Output '################ PRUEBA 1: salida del sistema + app A + app B + microfono, TODO A LA VEZ ################'
& .\multi.exe $a.Id $b.Id 4

Stop-Process -Id $a.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $b.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

$wa = Get-Process | Where-Object { $_.ProcessName -like '*Whats*' } | Select-Object -First 1
$c = Start-Player $wavA
Start-Sleep -Milliseconds 1200
Write-Output ''
Write-Output '################ PRUEBA 2: salida del sistema + WhatsApp + un emisor + microfono ################'
if ($wa) {
    Write-Output "WhatsApp: $($wa.ProcessName) PID $($wa.Id)"
    & .\multi.exe $wa.Id $c.Id 4
} else {
    Write-Output 'WhatsApp no esta corriendo'
}
Stop-Process -Id $c.Id -Force -ErrorAction SilentlyContinue
Write-Output ''
Write-Output 'fin'
