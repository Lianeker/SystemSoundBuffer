# El limite izquierdo del buffer: ¿cubre de verdad lo que se pide?
#
#     .\probes\run-buffer-limite.ps1 -Indice 4 -Segundos 420
#
# El usuario pidio 5 minutos y vio que lo maximo seleccionable eran 172 s, luego
# 257 s, con el limite izquierdo pegando saltos. Esto graba mas de lo que dura el
# buffer y va anotando lo que cubre, para ver si se estabiliza en lo pedido o por
# debajo.
#
# La fuente es una ENTRADA y no la salida a proposito: un dispositivo de entrada
# entrega paquetes siempre, suene algo o no. El loopback de la salida deja de
# entregar cuando el dispositivo se queda inactivo, y entonces lo que se mide es
# el silencio del sistema y no el buffer.
param([int]$Indice = 4, [int]$Segundos = 420, [int]$Cada = 30)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer -EA SilentlyContinue

$esperado = @(10, 30, 60, 120, 300, 900, 1800, 3600, 7200)[$Indice]

$lineas = @("add input", "wait 1", "rec")
$n = [math]::Floor($Segundos / $Cada)
for ($i = 0; $i -lt $n; $i++) { $lineas += "wait $Cada"; $lineas += "tracks" }
$lineas += @("stop", "quit")
$lineas | Set-Content -Path limite.ssb -Encoding Ascii

# --buffer fija la duracion ANTES de crear ninguna pista, que es lo que importa:
# el indice del anillo se dimensiona al crear la pista.
$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @(
    '--mode','cmd','--buffer',"$Indice",'--script','limite.ssb')
if (-not $gui.WaitForExit(($Segundos + 90) * 1000)) { Stop-Process -Id $gui.Id -Force -EA SilentlyContinue }

Write-Output "pedido: $esperado s (indice $Indice)"
Write-Output ""
$t = 0
Get-Content limite.ssb.log -EA SilentlyContinue | Select-String -Pattern 'cubre' | ForEach-Object {
    $t += $Cada
    if ($_ -match 'cubre\s+([\d\.]+) s') {
        $c = [double]$matches[1]
        Write-Output ("  t={0,4} s   cubre {1,7:N2} s   {2}" -f $t, $c, $(if ($c -ge $esperado) { "ok" } else { "por debajo de lo pedido" }))
    }
}
