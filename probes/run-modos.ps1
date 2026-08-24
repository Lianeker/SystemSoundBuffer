# Modo comandos: se maneja todo por teclado, sin tocar el raton.
# Modo reducido: dibuja lo minimo.
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer -EA SilentlyContinue

$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; 1..60 | ForEach-Object { `$p.PlaySync() }")
Start-Sleep -Milliseconds 700

# Arranca SIN pistas y en modo comandos: todo lo demas se hace escribiendo.
$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--theme','dark','--mode','cmd')
Start-Sleep -Seconds 3

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$src = @'
using System;
using System.Runtime.InteropServices;
public class WM {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int t, bool p);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
'@
if (-not ('WM' -as [type])) { Add-Type -TypeDefinition $src }
[void][WM]::SetProcessDPIAware()
[void][WM]::MoveWindow($gui.MainWindowHandle, 20, 20, 1200, 640, $true)
Start-Sleep -Milliseconds 800
$r = New-Object WM+RECT
[void][WM]::GetWindowRect($gui.MainWindowHandle, [ref]$r)
$w = $r.R - $r.L; $h = $r.B - $r.T
function Shot($n) {
    $b = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($b)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $b.Size); $g.Dispose()
    $b.Save((Join-Path (Get-Location) $n), [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
}
function Cmd($text) {
    [void][WM]::SetForegroundWindow($gui.MainWindowHandle)
    Start-Sleep -Milliseconds 350
    [System.Windows.Forms.SendKeys]::SendWait("$text{ENTER}")
    Start-Sleep -Milliseconds 900
}

Write-Output 'todo por comandos: add, buffer, rec, sel, save'
Cmd 'help'
Cmd 'add output'
Cmd ("add app:" + $p1.Id)
Cmd 'buffer 20'
Cmd 'rec'
Start-Sleep -Seconds 10
Cmd 'tracks'
Shot 'md-cmd.png'
Cmd 'stop'
Cmd 'save 6'
Start-Sleep -Milliseconds 1200
Shot 'md-cmd2.png'

Write-Output ''
Write-Output 'modo reducido (Ctrl+M)'
[void][WM]::SetForegroundWindow($gui.MainWindowHandle)
Start-Sleep -Milliseconds 300
[System.Windows.Forms.SendKeys]::SendWait('^m')
Start-Sleep -Milliseconds 900
[void][WM]::MoveWindow($gui.MainWindowHandle, 20, 20, 1200, 330, $true)
Start-Sleep -Milliseconds 700
$b2 = New-Object System.Drawing.Bitmap 1200, 330
$g2 = [System.Drawing.Graphics]::FromImage($b2)
$g2.CopyFromScreen($r.L, $r.T, 0, 0, $b2.Size); $g2.Dispose()
$b2.Save((Join-Path (Get-Location) 'md-small.png'), [System.Drawing.Imaging.ImageFormat]::Png); $b2.Dispose()
Write-Output 'md-small.png'

Write-Output ''
Write-Output '=== ficheros guardados por comando ==='
Get-ChildItem ssb-gui-buffer\*.wav -EA SilentlyContinue |
    ForEach-Object { Write-Output ("  {0,-30} {1,9:N0} bytes" -f $_.Name, $_.Length) }

Stop-Process -Id $gui.Id -Force -EA SilentlyContinue
Stop-Process -Id $p1.Id -Force -EA SilentlyContinue
