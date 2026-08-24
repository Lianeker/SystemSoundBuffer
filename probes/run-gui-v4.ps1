# Comprueba el modelo nuevo: nada se graba hasta pulsar Grabar, los botones se
# ven enteros, y la rueda desplaza de verdad entre pistas.
param([string]$Theme = 'dark')
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')

$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; 1..60 | ForEach-Object { `$p.PlaySync() }")
Start-Sleep -Milliseconds 700
$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @(
    '--theme', $Theme, '--buffer', '1',
    '--src', 'output', '--src', "app:$($p1.Id)", '--src', 'input', '--src', "app:$($p1.Id)")
Start-Sleep -Seconds 4

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$src = @'
using System;
using System.Runtime.InteropServices;
public class W8 {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int t, bool p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public const uint WHEEL=0x0800;
}
'@
if (-not ('W8' -as [type])) { Add-Type -TypeDefinition $src }
[void][W8]::SetProcessDPIAware()
[void][W8]::MoveWindow($gui.MainWindowHandle, 20, 20, 1320, 640, $true)
[void][W8]::SetForegroundWindow($gui.MainWindowHandle)
Start-Sleep -Milliseconds 1000
$r = New-Object W8+RECT
[void][W8]::GetWindowRect($gui.MainWindowHandle, [ref]$r)
$w = $r.R - $r.L; $h = $r.B - $r.T
function Shot($n) {
    $b = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($b)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $b.Size); $g.Dispose()
    $b.Save((Join-Path (Get-Location) $n), [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
}

Shot 'v4-listo.png'
Write-Output 'v4-listo.png : cuatro pistas anadidas, NADA deberia estar grabando'

[System.Windows.Forms.SendKeys]::SendWait('^p')
Start-Sleep -Seconds 14
Shot 'v4-grabando.png'
Write-Output 'v4-grabando.png : tras pulsar Grabar (Ctrl+P) y 14 s'

[void][W8]::SetCursorPos($r.L + 600, $r.T + 400)
Start-Sleep -Milliseconds 300
for ($i=0; $i -lt 6; $i++) { [W8]::mouse_event([W8]::WHEEL, 0, 0, [uint32]4294967176, [IntPtr]::Zero); Start-Sleep -Milliseconds 130 }
Start-Sleep -Milliseconds 700
Shot 'v4-scroll.png'
Write-Output 'v4-scroll.png : tras seis muescas de rueda hacia abajo'

Stop-Process -Id $gui.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $p1.Id -Force -ErrorAction SilentlyContinue
