# Agrandar el buffer con la pista YA creada y grabando.
#
#     .\probes\run-buffer-agrandar.ps1
#
# El indice del anillo (`r->cap`) se dimensiona al crear la pista y
# `ssb_ring_set_seconds` no lo toca. Si eso importa, al subir la duracion el
# buffer no llegara a lo nuevo: se quedara donde el indice viejo le permita, y
# descartara por "indice lleno", que es un descarte FORZADO que se salta la
# regla de no bajar de lo pedido.
#
# Entrada y no salida: una entrada entrega paquetes suene algo o no.
param([int]$Antes = 3, [int]$Despues = 4, [int]$Llenar = 160, [int]$Crecer = 360, [int]$Cada = 30)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer -EA SilentlyContinue

$secs = @(10, 30, 60, 120, 300, 900, 1800, 3600, 7200)
$vAntes = $secs[$Antes]
$vDespues = $secs[$Despues]

$lineas = @("add input", "wait 1", "rec", "wait $Llenar", "tracks", "buffer $vDespues")
$n = [math]::Floor($Crecer / $Cada)
for ($i = 0; $i -lt $n; $i++) { $lineas += "wait $Cada"; $lineas += "tracks" }
$lineas += @("stop", "quit")
$lineas | Set-Content -Path agrandar.ssb -Encoding Ascii

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @(
    '--mode','cmd','--buffer',"$Antes",'--script','agrandar.ssb')
if (-not $gui.WaitForExit(($Llenar + $Crecer + 120) * 1000)) { Stop-Process -Id $gui.Id -Force -EA SilentlyContinue }

Write-Output "pista creada con $vAntes s; a mitad de grabacion se pide $vDespues s"
Write-Output ""
$t = $Llenar
Get-Content agrandar.ssb.log -EA SilentlyContinue | Select-String -Pattern 'cubre' | ForEach-Object {
    if ($_ -match 'cubre\s+([\d\.,]+) s de (\d+)') {
        $c = [double]($matches[1] -replace ',', '.')
        $pedido = [int]$matches[2]
        Write-Output ("  t={0,4} s   pedido {1,4} s   cubre {2,7:N2} s   {3}" -f $t, $pedido, $c, $(if ($c -ge $pedido) { "ok" } else { "CORTO" }))
        $t += $Cada
    }
}
