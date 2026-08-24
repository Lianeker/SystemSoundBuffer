# Anadir una pista A MITAD de la grabacion.
#
# Lo que hay que demostrar:
#   - que se puede seleccionar lo anterior a esa pista
#   - que se exporta, y que TODAS las pistas salen con la misma duracion
#   - que la pista tardia trae silencio en el tramo en que no existia
param([double]$Antes = 12, [double]$Despues = 10)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer, tarde -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path tarde | Out-Null
$dest = (Resolve-Path tarde).Path

$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; `$p.PlayLooping(); Start-Sleep -Seconds $($Antes + $Despues + 50)")
Start-Sleep -Milliseconds 900

@(
    "quality 16",
    "mix off",
    "add output",
    "buffer 120",
    "folder $dest",
    "export wav",
    "wait 1",
    "rec",
    "wait $Antes",
    "add app:$($p1.Id)",
    "wait $Despues",
    "stop",
    "wait 1",
    "all",
    "save",
    "tracks",
    "wait 2",
    "play",
    "wait 3",
    "hush",
    "wait 2",
    "quit"
) | Set-Content -Path tarde.ssb -Encoding Ascii

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--mode','cmd','--script','tarde.ssb')
if (-not $gui.WaitForExit(($Antes + $Despues + 50) * 1000)) { Stop-Process -Id $gui.Id -Force -EA SilentlyContinue }
Stop-Process -Id $p1.Id -Force -EA SilentlyContinue

Write-Output '=== registro ==='
Get-Content tarde.ssb.log -EA SilentlyContinue | ForEach-Object { Write-Output "  $_" }
Write-Output ''
Write-Output '=== ficheros ==='
Get-ChildItem $dest -EA SilentlyContinue | ForEach-Object { Write-Output ("  {0,-34} {1,11:N0} bytes" -f $_.Name, $_.Length) }
