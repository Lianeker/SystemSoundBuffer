# Arranca la interfaz, graba unos segundos y simula un arrastre real del raton
# sobre la onda para comprobar la seleccion de extremo a extremo.
param([double]$Secs = 12, [string]$Png = 'gui-sel.png')
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')

$wav = 'C:\Windows\Media\Alarm01.wav'
$player = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command',
    "`$p = New-Object Media.SoundPlayer '$wav'; 1..40 | ForEach-Object { `$p.PlaySync() }"
)
Start-Sleep -Milliseconds 800
$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @(
    '--export', 'mp3', '--src', 'output', '--src', "app:$($player.Id)", '--src', 'input')
Start-Sleep -Seconds $Secs

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$src = @'
using System;
using System.Runtime.InteropServices;
public class W2 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int t, bool p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public const uint DOWN = 0x0002, UP = 0x0004;
}
'@
if (-not ('W2' -as [type])) { Add-Type -TypeDefinition $src }

[void][W2]::MoveWindow($gui.MainWindowHandle, 20, 20, 1440, 800, $true)
[void][W2]::SetForegroundWindow($gui.MainWindowHandle)
Start-Sleep -Milliseconds 1000

$r = New-Object W2+RECT
[void][W2]::GetWindowRect($gui.MainWindowHandle, [ref]$r)

# Arrastre sobre la banda de la primera pista, de un tercio a dos tercios.
$y  = $r.T + 320
$x1 = $r.L + 420
$x2 = $r.L + 980
Write-Output "arrastrando de ($x1,$y) a ($x2,$y)"
[void][W2]::SetCursorPos($x1, $y)
Start-Sleep -Milliseconds 200
[W2]::mouse_event([W2]::DOWN, 0, 0, 0, [IntPtr]::Zero)
for ($x = $x1; $x -le $x2; $x += 40) {
    [void][W2]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 40
}
[void][W2]::SetCursorPos($x2, $y)
Start-Sleep -Milliseconds 120
[W2]::mouse_event([W2]::UP, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 700

# Guardado rapido con Ctrl+S: no abre dialogo, escribe en el directorio del buffer.
Write-Output ''
Write-Output '=== Ctrl+S sobre la seleccion ==='
[void][W2]::SetForegroundWindow($gui.MainWindowHandle)
Start-Sleep -Milliseconds 400
[System.Windows.Forms.SendKeys]::SendWait('^s')
Start-Sleep -Milliseconds 1500
Get-ChildItem ssb-gui-buffer\seleccion-*.wav -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Output ("  {0,-24} {1,10:N0} bytes" -f $_.Name, $_.Length)
}


$w = $r.R - $r.L; $h = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$g.Dispose()
$bmp.Save((Join-Path (Get-Location) $Png), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "captura en $Png"

Stop-Process -Id $gui.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $player.Id -Force -ErrorAction SilentlyContinue
