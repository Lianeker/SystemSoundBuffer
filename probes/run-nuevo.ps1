# Las tres funciones nuevas, de punta a punta y sin tocar el raton:
#   - lista de canales: que app suena y por que salida
#   - exportar todas las pistas en UN fichero
#   - reproducir lo seleccionado, con pausa
param([double]$Secs = 14)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer, nv -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path nv | Out-Null
$dest = (Resolve-Path nv).Path

$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; `$p.PlayLooping(); Start-Sleep -Seconds $($Secs + 70)")
Start-Sleep -Milliseconds 900

@(
    "list",
    "add output",
    "add app:$($p1.Id)",
    "buffer 120",
    "folder $dest",
    "export wav",
    "quality 24",
    "wait 1",
    "rec",
    "wait $Secs",
    "stop",
    "wait 1",
    "all",
    "mix on",
    "save",
    "wait 3",
    "play",
    "wait 3",
    "play",
    "wait 2",
    "play",
    "wait 6",
    "hush",
    "tracks",
    "wait 2",
    "quit"
) | Set-Content -Path nv.ssb -Encoding Ascii

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--mode','cmd','--script','nv.ssb')
if (-not $gui.WaitForExit(($Secs + 70) * 1000)) { Stop-Process -Id $gui.Id -Force -EA SilentlyContinue }
Stop-Process -Id $p1.Id -Force -EA SilentlyContinue

Write-Output '=== registro ==='
Get-Content nv.ssb.log -EA SilentlyContinue | ForEach-Object { Write-Output "  $_" }
Write-Output ''
Write-Output '=== ficheros ==='
Get-ChildItem $dest -EA SilentlyContinue | ForEach-Object { Write-Output ("  {0,-34} {1,11:N0} bytes" -f $_.Name, $_.Length) }
