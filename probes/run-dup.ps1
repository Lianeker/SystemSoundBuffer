# ¿Varios clientes de loopback sobre el MISMO dispositivo de salida reciben todos
# el audio, o solo uno? Importa porque la interfaz permite añadir la misma fuente
# dos veces sin avisar.
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
$wav = 'C:\Windows\Media\Alarm01.wav'
$p = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command',
    "`$s = New-Object Media.SoundPlayer '$wav'; 1..20 | ForEach-Object { `$s.PlaySync() }")
Start-Sleep -Milliseconds 1000
& .\ssb.exe rec --secs 6 --buffer 30 --out dup --src output --src output --src "app:$($p.Id)"
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
