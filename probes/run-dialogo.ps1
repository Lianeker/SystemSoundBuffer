# El camino que fallaba: boton "Save selection..." con el DIALOGO, guardando en
# otra carpeta (que es lo que cambia el directorio de trabajo del proceso).
param([string]$Fmt = 'mp3')
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer, dlg -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path dlg | Out-Null
$dest = (Resolve-Path dlg).Path

$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; 1..60 | ForEach-Object { `$p.PlaySync() }")
Start-Sleep -Milliseconds 700

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @(
    '--theme','dark','--mode','cmd','--export',$Fmt)
Start-Sleep -Seconds 3

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$src = @'
using System;
using System.Runtime.InteropServices;
public class WD {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int t, bool p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public const uint DOWN=0x0002, UP=0x0004;
}
'@
if (-not ('WD' -as [type])) { Add-Type -TypeDefinition $src }
[void][WD]::SetProcessDPIAware()
[void][WD]::MoveWindow($gui.MainWindowHandle, 20, 20, 1320, 700, $true)
[void][WD]::SetForegroundWindow($gui.MainWindowHandle)
Start-Sleep -Milliseconds 800
$r = New-Object WD+RECT
[void][WD]::GetWindowRect($gui.MainWindowHandle, [ref]$r)

# El montaje va por COMANDOS, que son deterministas; el raton solo para el dialogo.
function Cmd($t) {
    [void][WD]::SetForegroundWindow($gui.MainWindowHandle)
    Start-Sleep -Milliseconds 350
    [System.Windows.Forms.SendKeys]::SendWait("$t{ENTER}")
    Start-Sleep -Milliseconds 800
}
Cmd 'add output'
Cmd ("add app:" + $p1.Id)
Cmd 'buffer 30'
Cmd 'rec'
Start-Sleep -Seconds 12
Cmd 'stop'
Cmd 'all'

# Clic en "Save selection..." (segunda fila de la barra)
Write-Output 'clic en "Save selection..."'
[void][WD]::SetCursorPos($r.L + 888, $r.T + 103)
Start-Sleep -Milliseconds 300
[WD]::mouse_event([WD]::DOWN,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 90
[WD]::mouse_event([WD]::UP,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 1800

# En el dialogo: escribir una ruta ABSOLUTA a otra carpeta y aceptar.
Write-Output "en el dialogo se escribe: $dest\prueba.$Fmt"
[System.Windows.Forms.SendKeys]::SendWait("$dest\prueba.$Fmt")
Start-Sleep -Milliseconds 600
[System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
Start-Sleep -Seconds 3

Write-Output ''
Write-Output '=== ficheros en la carpeta elegida ==='
Get-ChildItem dlg | ForEach-Object { Write-Output ("  {0,-26} {1,9:N0} bytes" -f $_.Name, $_.Length) }

$b = New-Object System.Drawing.Bitmap ($r.R-$r.L), ($r.B-$r.T)
$g = [System.Drawing.Graphics]::FromImage($b)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $b.Size); $g.Dispose()
$b.Save((Join-Path (Get-Location) 'dlg.png'), [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()

Stop-Process -Id $gui.Id -Force -EA SilentlyContinue
Stop-Process -Id $p1.Id -Force -EA SilentlyContinue
