# Anadir una pista a mitad de la grabacion y luego MEZCLAR.
#
# Dos sintomas que el usuario ve y que pueden ser el mismo fallo:
#   - no se puede reproducir lo seleccionado, aunque lo exportado esta bien
#   - "un solo fichero" deja varios ficheros, y distintos
#
# La hipotesis es que ssb_mix_wavs falla y nadie limpia: los trozos por pista se
# quedan en la carpeta (de ahi "varios ficheros, y distintos") y la reproduccion,
# que usa el mismo mezclador, no puede sonar.
param([double]$Antes = 10, [double]$Despues = 8)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer, mezcla -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path mezcla | Out-Null
$dest = (Resolve-Path mezcla).Path

$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; `$p.PlayLooping(); Start-Sleep -Seconds $($Antes + $Despues + 60)")
Start-Sleep -Milliseconds 900

@(
    "quality 16",
    "add output",
    "buffer 120",
    "folder $dest",
    "export wav",
    "mix on",
    "wait 1",
    "rec",
    "wait $Antes",
    "add app:$($p1.Id)",
    "wait $Despues",
    "stop",
    "wait 1",
    "tracks",
    "all",
    "save",
    "wait 2",
    "play",
    "wait 3",
    "hush",
    "wait 2",
    "quit"
) | Set-Content -Path mezcla.ssb -Encoding Ascii

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--mode','cmd','--script','mezcla.ssb')
if (-not $gui.WaitForExit(($Antes + $Despues + 60) * 1000)) { Stop-Process -Id $gui.Id -Force -EA SilentlyContinue }
Stop-Process -Id $p1.Id -Force -EA SilentlyContinue

Write-Output '=== registro ==='
Get-Content mezcla.ssb.log -EA SilentlyContinue | ForEach-Object { Write-Output "  $_" }
Write-Output ''
Write-Output '=== ficheros en la carpeta de destino ==='
Get-ChildItem $dest -EA SilentlyContinue | ForEach-Object { Write-Output ("  {0,-38} {1,11:N0} bytes" -f $_.Name, $_.Length) }
