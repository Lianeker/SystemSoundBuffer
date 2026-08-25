# La cifra "disco" contra los bytes que hay de verdad en el directorio.
#
#     .\probes\run-disco-crece.ps1 [-Secs 45] [-Buffer 10]
#
# Se graba con un buffer corto para que el circular tenga que descartar de
# verdad, y cada segundo se mide el tamano real de la carpeta de segmentos. Al
# final se compara con lo que el motor dice en `disco N KB`.
#
# Lo que se espera si el circular funciona: la carpeta se estabiliza en el
# tamano del buffer y deja de crecer. Solo se cuentan los segmentos: el WAV
# que se exporta al final vive en el mismo directorio y no es buffer.
param(
    [double]$Secs = 45,
    [int]$Buffer = 10,
    [string]$Exe = ''
)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..')
if ($Exe -eq '') { $Exe = (Resolve-Path '.\build\ssb.exe').Path }

$dir = Join-Path $env:TEMP 'ssb-disco'
Remove-Item -Recurse -Force $dir -EA SilentlyContinue

$salida = Join-Path $env:TEMP 'ssb-disco.txt'
$p = Start-Process $Exe -PassThru -NoNewWindow -RedirectStandardOutput $salida `
     -ArgumentList @('rec', '--secs', $Secs, '--buffer', $Buffer, '--segment-kb', '512', '--out', $dir)

$reloj = [Diagnostics.Stopwatch]::StartNew()
$muestras = @()
while (-not $p.HasExited) {
    $b = 0
    if (Test-Path $dir) {
        $b = (Get-ChildItem -Recurse -File -Filter seg-*.dat $dir -EA SilentlyContinue |
              Measure-Object -Property Length -Sum).Sum
        if ($null -eq $b) { $b = 0 }
    }
    $muestras += [pscustomobject]@{ s = [math]::Round($reloj.Elapsed.TotalSeconds, 1); kb = [math]::Round($b / 1024.0, 1) }
    Start-Sleep -Milliseconds 1000
}

Write-Output ("buffer pedido: {0} s" -f $Buffer)
Write-Output ""
Write-Output "  t(s)   carpeta(KB)"
foreach ($m in $muestras) { Write-Output ("  {0,5}  {1,10}" -f $m.s, $m.kb) }
Write-Output ""
Write-Output "lo que dice el motor:"
Get-Content $salida | Select-String 'disco|bloques' | ForEach-Object { Write-Output ("  " + $_.Line.Trim()) }

Remove-Item -Recurse -Force $dir -EA SilentlyContinue
Remove-Item -Force $salida -EA SilentlyContinue
