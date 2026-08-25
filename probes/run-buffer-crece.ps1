# Agrandar el buffer MIENTRAS se graba.
#
#     .\probes\run-buffer-crece.ps1
#
# Lo que hay que demostrar: que tras subir la duracion, el buffer llega de
# verdad a la nueva duracion. El indice del anillo se dimensiona al crear la
# pista; si al agrandar no crece, el anillo se queda con la capacidad vieja y
# empieza a descartar por indice lleno — que es un descarte FORZADO y se salta
# la regla de no bajar de lo pedido.
#
# Se hace con numeros pequenos para que se vea en dos minutos en vez de en diez.
param([int]$Chico = 20, [int]$Grande = 120, [int]$Llenar = 45, [int]$Crecer = 100)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer -EA SilentlyContinue

$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; `$p.PlayLooping(); Start-Sleep -Seconds $($Llenar + $Crecer + 60)")
Start-Sleep -Milliseconds 900

# `tracks` va imprimiendo lo que cubre el buffer; se pide varias veces para ver
# la evolucion y no solo el final.
$lineas = @(
    # El buffer se fija ANTES de crear la pista: el indice del anillo se
    # dimensiona al crearla, asi que crearla ya con el valor chico es lo que
    # pone a prueba el crecimiento posterior.
    "buffer $Chico",
    "add output",
    "wait 1",
    "rec",
    "wait $Llenar",
    "tracks",
    "buffer $Grande",
    "wait 20", "tracks",
    "wait 20", "tracks",
    "wait 20", "tracks",
    "wait 20", "tracks",
    "wait 20", "tracks",
    "stop",
    "quit")
$lineas | Set-Content -Path crece.ssb -Encoding Ascii

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--mode','cmd','--script','crece.ssb')
if (-not $gui.WaitForExit(($Llenar + $Crecer + 90) * 1000)) { Stop-Process -Id $gui.Id -Force -EA SilentlyContinue }
Stop-Process -Id $p1.Id -Force -EA SilentlyContinue

Write-Output "pedido: $Chico s, luego $Grande s"
Write-Output ""
Get-Content crece.ssb.log -EA SilentlyContinue |
    Select-String -Pattern 'cubre|seleccionable|buffer:' |
    ForEach-Object { Write-Output "  $_" }
