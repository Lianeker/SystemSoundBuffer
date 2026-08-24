# Mezclar una fuente MONO con una ESTEREO.
#
# Un microfono es mono y una salida es estereo: grabarlos juntos es lo normal, y
# hasta ahora el mezclador se negaba. El resultado era que en cuanto anadias un
# microfono no se podia exportar en un fichero ni reproducir nada, y ademas los
# trozos por pista se quedaban en la carpeta sin decirlo.
#
# Lo que hay que demostrar no es solo que salga UN fichero: es que ese fichero
# CONTIENE las dos pistas. Un mezclador que devuelve silencio tambien devuelve
# un solo fichero.
#
# Por eso se exporta dos veces sobre LA MISMA seleccion —aparte y junto— y luego
# se comprueba que la suma de las partes es la mezcla.
param([double]$Antes = 6, [double]$Despues = 6)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer, mono-aparte, mono-junto -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path mono-aparte, mono-junto | Out-Null
# Dos carpetas: las dos exportaciones comparten el sello de tiempo y, en la
# misma carpeta, la mezcla se lleva por delante los trozos que iba a comparar.
$dest = (Resolve-Path mono-aparte).Path
$dest2 = (Resolve-Path mono-junto).Path

$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; `$p.PlayLooping(); Start-Sleep -Seconds $($Antes + $Despues + 60)")
Start-Sleep -Milliseconds 900

@(
    "quality 16",
    "add output:0",
    "buffer 120",
    "folder $dest",
    "export wav",
    "wait 1",
    "rec",
    "wait $Antes",
    "add input:1",
    "wait $Despues",
    "stop",
    "wait 1",
    "tracks",
    "all",
    # La MISMA seleccion, exportada de las dos formas.
    "mix off",
    "save",
    "wait 1",
    "folder $dest2",
    "mix on",
    "save",
    "wait 2",
    "play",
    "wait 3",
    "hush",
    "wait 1",
    "quit"
) | Set-Content -Path mono.ssb -Encoding Ascii

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--mode','cmd','--script','mono.ssb')
if (-not $gui.WaitForExit(($Antes + $Despues + 60) * 1000)) { Stop-Process -Id $gui.Id -Force -EA SilentlyContinue }
Stop-Process -Id $p1.Id -Force -EA SilentlyContinue

Write-Output '=== registro ==='
Get-Content mono.ssb.log -EA SilentlyContinue | Select-Object -Last 14 | ForEach-Object { Write-Output "  $_" }
Write-Output ''
Write-Output '=== por separado ==='
Get-ChildItem $dest -EA SilentlyContinue | ForEach-Object { Write-Output ("  {0,-40} {1,11:N0} bytes" -f $_.Name, $_.Length) }
Write-Output '=== en un fichero ==='
Get-ChildItem $dest2 -EA SilentlyContinue | ForEach-Object { Write-Output ("  {0,-40} {1,11:N0} bytes" -f $_.Name, $_.Length) }
