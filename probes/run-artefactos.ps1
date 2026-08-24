# Mide los artefactos de colocacion: silencio insertado en mitad de la onda.
#
# Ni una pulsacion simulada. El programa se maneja con --script, que ejecuta las
# ordenes desde dentro y no depende de que la ventana tenga el foco. Cuatro
# depuraciones falsas costo aprender eso.
param([double]$Secs = 45, [int]$Bits = 16)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer, art -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path art | Out-Null
$dest = (Resolve-Path art).Path

# Tono continuo: un unico flujo de render que no se abre ni se cierra, para que
# no haya transiciones que justifiquen un hueco de verdad.
$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; `$p.PlayLooping(); Start-Sleep -Seconds $($Secs + 40)")
Start-Sleep -Milliseconds 900

$script = Join-Path (Get-Location) 'art.ssb'
@(
    "quality $Bits",
    "mix off",
    "add output",
    "add app:$($p1.Id)",
    "buffer 120",
    "folder $dest",
    "export wav",
    "wait 1",
    "rec",
    "wait $Secs",
    "stop",
    "wait 1",
    "all",
    "save",
    "tracks",
    "wait 8",
    "quit"
) | Set-Content -Path $script -Encoding UTF8

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--theme','dark','--mode','cmd','--script',$script)
Write-Output "grabando $Secs s (guion: $script)"

# Nada de capturas de pantalla: fotografian lo que haya encima de la ventana
# (una vez salio una conversacion ajena) y mienten cuando otra app roba el
# primer plano. El programa escribe su propia consola en <guion>.log.
Start-Sleep -Seconds ($Secs + 8)

if (-not $gui.WaitForExit(($Secs + 40) * 1000)) {
    Write-Output 'el programa no termino solo; se cierra'
    Stop-Process -Id $gui.Id -Force -EA SilentlyContinue
}
Stop-Process -Id $p1.Id -Force -EA SilentlyContinue

Write-Output ''
Write-Output '=== exportado ==='
$got = Get-ChildItem "$dest\*.wav" -EA SilentlyContinue
if ($got) { $got | ForEach-Object { Write-Output ("  {0,-34} {1,11:N0} bytes" -f $_.Name, $_.Length) } }
else { Write-Output '  NADA - el guion no llego a guardar' }

# Y la medida, aqui mismo. Antes se hacia a mano con un fragmento de Python que
# se reescribia cada vez, y un dia se colo con el umbral a 1 frame: marco como
# interrupcion un cruce por cero de 0.02 ms y me hizo buscar una regresion que
# no existia. Una medida que se reescribe cada vez no es una medida.
Write-Output ''
Write-Output '=== interrupciones ==='
python (Join-Path $PSScriptRoot 'artefactos.py') $dest
