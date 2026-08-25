# Cuanto dura el destello blanco al arrancar.
#
#     .\probes\run-arranque-blanco.ps1
#
# Fotografia la ventana cada pocos milisegundos desde que aparece y mide, en cada
# toma, que porcentaje del lienzo de ondas sigue siendo blanco. Asi el "tarda un
# poco en cargar" deja de ser una impresion y pasa a ser un numero.
#
# Con PrintWindow, que le pide a la ventana que se dibuje: lo que sale es esta
# ventana y no lo que hubiera encima.
param([int]$Tomas = 24, [int]$CadaMs = 120)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer, arranque -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path arranque | Out-Null
# Ruta absoluta: .NET no comparte el directorio actual con PowerShell, asi que
# una ruta relativa la resuelve contra otro sitio y Save falla con un
# "error generico en GDI+" que no dice nada.
$dirTomas = (Resolve-Path arranque).Path

@('wait 30', 'quit') | Set-Content -Path arranque.ssb -Encoding Ascii

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class AB {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RC r);
  [StructLayout(LayoutKind.Sequential)] public struct RC { public int L,T,R,B; }
}
"@
[void][AB]::SetProcessDPIAware()

$reloj = [Diagnostics.Stopwatch]::StartNew()
$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--lang','en','--script','arranque.ssb')

# Esperar a que exista ventana, sin dormir de mas: lo que se mide es el arranque.
$h = [IntPtr]::Zero
while ($reloj.ElapsedMilliseconds -lt 8000 -and $h -eq [IntPtr]::Zero) {
    $gui.Refresh()
    $h = $gui.MainWindowHandle
    if ($h -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 15 }
}
$aparece = $reloj.ElapsedMilliseconds
Write-Output ("ventana visible a los {0} ms" -f $aparece)
Write-Output ""

for ($i = 0; $i -lt $Tomas; $i++) {
    $ms = $reloj.ElapsedMilliseconds
    $r = New-Object AB+RC
    [void][AB]::GetWindowRect($h, [ref]$r)
    $w = $r.R - $r.L; $ht = $r.B - $r.T
    if ($w -gt 0 -and $ht -gt 0) {
        $b = New-Object System.Drawing.Bitmap $w, $ht
        $g = [System.Drawing.Graphics]::FromImage($b)
        $hdc = $g.GetHdc()
        [void][AB]::PrintWindow($h, $hdc, 2)
        $g.ReleaseHdc($hdc); $g.Dispose()
        $b.Save((Join-Path $dirTomas ("t{0:0000}.png" -f $ms)), [System.Drawing.Imaging.ImageFormat]::Png)
        $b.Dispose()
    }
    Start-Sleep -Milliseconds $CadaMs
}
Stop-Process -Id $gui.Id -Force -EA SilentlyContinue

# El analisis, en Python: contar blanco es mas claro ahi que en PowerShell.
python (Join-Path $PSScriptRoot 'arranque.py') $dirTomas
