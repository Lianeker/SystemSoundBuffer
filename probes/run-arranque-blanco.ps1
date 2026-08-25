# Cuanto dura el destello blanco al arrancar.
#
#     .\probes\run-arranque-blanco.ps1 [-Exe ruta] [-Ms 1500]
#
# No se puede usar PrintWindow: le pide a la ventana que se dibuje, o sea que
# fuerza el repintado y el destello desaparece. Se midio asi y salio "0,1 % de
# blanco desde el primer fotograma", que era mentira.
#
# Aqui se lee el DC de la propia ventana con BitBlt, que copia lo que hay en
# pantalla sin pedir nada. Se trae la ventana al frente antes, para que lo que
# se copie sea suyo y no de lo que tenga encima.
param(
    [string]$Exe = '',
    [int]$Ms = 1500,
    [int]$CadaMs = 25
)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..')
if ($Exe -eq '') { $Exe = (Resolve-Path '.\build\ssbgui.exe').Path }
$trabajo = Join-Path $env:TEMP 'ssb-arranque'
Remove-Item -Recurse -Force $trabajo -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path $trabajo | Out-Null

Set-Location (Split-Path $Exe -Parent)
Remove-Item -Recurse -Force ssb-gui-buffer -EA SilentlyContinue
@('wait 20', 'quit') | Set-Content -Path arranque.ssb -Encoding Ascii

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class BA {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr h);
  [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr dc);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RC r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr d, int x, int y, int w, int h, IntPtr s, int sx, int sy, int rop);
  [StructLayout(LayoutKind.Sequential)] public struct RC { public int L,T,R,B; }
}
"@
[void][BA]::SetProcessDPIAware()

$reloj = [Diagnostics.Stopwatch]::StartNew()
$gui = Start-Process $Exe -PassThru -ArgumentList @('--lang','en','--script','arranque.ssb')

$h = [IntPtr]::Zero
while ($reloj.ElapsedMilliseconds -lt 10000 -and $h -eq [IntPtr]::Zero) {
    $gui.Refresh(); $h = $gui.MainWindowHandle
}
$aparece = $reloj.ElapsedMilliseconds
[void][BA]::SetForegroundWindow($h)

$n = 0
while ($reloj.ElapsedMilliseconds -lt ($aparece + $Ms)) {
    $ms = $reloj.ElapsedMilliseconds
    $r = New-Object BA+RC
    if ([BA]::GetClientRect($h, [ref]$r) -and ($r.R - $r.L) -gt 0) {
        $w = $r.R - $r.L; $ht = $r.B - $r.T
        $src = [BA]::GetDC($h)
        $b = New-Object System.Drawing.Bitmap $w, $ht
        $g = [System.Drawing.Graphics]::FromImage($b)
        $dst = $g.GetHdc()
        [void][BA]::BitBlt($dst, 0, 0, $w, $ht, $src, 0, 0, 0x00CC0020)  # SRCCOPY
        $g.ReleaseHdc($dst); $g.Dispose()
        [void][BA]::ReleaseDC($h, $src)
        $b.Save((Join-Path $trabajo ("t{0:0000}.png" -f $ms)), [System.Drawing.Imaging.ImageFormat]::Png)
        $b.Dispose()
        $n++
    }
    Start-Sleep -Milliseconds $CadaMs
}
Stop-Process -Id $gui.Id -Force -EA SilentlyContinue

Write-Output ("ventana a los {0} ms; {1} tomas" -f $aparece, $n)
Write-Output ""
python (Join-Path $PSScriptRoot 'arranque.py') $trabajo

# Las tomas se borran: son de la pantalla, y de la pantalla no se guarda nada.
Remove-Item -Recurse -Force $trabajo -EA SilentlyContinue
