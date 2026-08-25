# ¿Cuanto tarda exportar, y cuesta huecos de captura?
#
#     .\probes\run-export-bloquea.ps1 -Indice 3
#
# Exportar escribe un WAV por pista y luego los suma, todo en el hilo de
# interfaz: de ahi el congelon al dar a Reproducir o a Guardar. Y `ssb_track_save_wav`
# lo hace con el mutex de la pista cogido, asi que mientras dura, la captura no
# puede entregar. Esto mide las dos cosas: el tiempo de pared del guardado y los
# huecos que anota el motor antes y despues.
param([int]$Indice = 3, [int]$Llenar = 130)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer, expo -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path expo | Out-Null
$dest = (Resolve-Path expo).Path
$secs = @(10, 30, 60, 120, 300, 900, 1800, 3600, 7200)[$Indice]

@(
    "add input",
    "add output",
    "folder $dest",
    "export wav",
    "mix off",
    "wait 1",
    "rec",
    "wait $Llenar",
    "tracks",
    "all",
    "perf",
    "save",
    "perf",
    "tracks",
    "wait 3",
    "tracks",
    "stop",
    "quit") | Set-Content -Path expo.ssb -Encoding Ascii

$reloj = [Diagnostics.Stopwatch]::StartNew()
$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @(
    '--mode','cmd','--buffer',"$Indice",'--script','expo.ssb')
if (-not $gui.WaitForExit(($Llenar + 120) * 1000)) { Stop-Process -Id $gui.Id -Force -EA SilentlyContinue }
$reloj.Stop()

$nominal = $Llenar + 4
Write-Output ("buffer $secs s; el guion pide $nominal s de espera y el proceso duro {0:N1} s" -f ($reloj.Elapsed.TotalSeconds))
Write-Output "  la diferencia es, sobre todo, lo que tardo el guardado"
Write-Output ""
Get-Content expo.ssb.log -EA SilentlyContinue | Select-String -Pattern 'huecos|Saved|Guardadas|perf|fps|ms' | ForEach-Object { Write-Output "  $_" }
Write-Output ""
Get-ChildItem $dest -EA SilentlyContinue | ForEach-Object { Write-Output ("  {0,-34} {1,11:N0} bytes" -f $_.Name, $_.Length) }
