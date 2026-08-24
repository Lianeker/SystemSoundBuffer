# Quitar una pista y anadir otra NO debe reutilizar la carpeta de una pista viva:
# dos anillos escribiendo los mismos seg-*.dat se pisan.
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer -EA SilentlyContinue

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @(
    '--theme', 'dark', '--buffer', '0', '--src', 'output', '--src', 'input', '--src', 'output')
Start-Sleep -Seconds 3

Add-Type -AssemblyName System.Drawing
$src = @'
using System;
using System.Runtime.InteropServices;
public class WC {
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
if (-not ('WC' -as [type])) { Add-Type -TypeDefinition $src }
[void][WC]::SetProcessDPIAware()
[void][WC]::MoveWindow($gui.MainWindowHandle, 20, 20, 1320, 700, $true)
[void][WC]::SetForegroundWindow($gui.MainWindowHandle)
Start-Sleep -Milliseconds 900
$r = New-Object WC+RECT
[void][WC]::GetWindowRect($gui.MainWindowHandle, [ref]$r)

function Click($x, $y) {
    [void][WC]::SetForegroundWindow($gui.MainWindowHandle)
    [void][WC]::SetCursorPos($r.L + $x, $r.T + $y)
    Start-Sleep -Milliseconds 250
    [WC]::mouse_event([WC]::DOWN,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 90
    [WC]::mouse_event([WC]::UP,0,0,0,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 600
}

Write-Output "carpetas tras arrancar con 3 pistas:"
(Get-ChildItem ssb-gui-buffer -Directory).Name -join ', '

Write-Output 'clic en "Remove last"'
Click 614 64
Write-Output 'clic en "Add track"'
Click 503 64

Write-Output ''
Write-Output 'carpetas al final (no debe repetirse ninguna con una pista viva):'
(Get-ChildItem ssb-gui-buffer -Directory).Name -join ', '

Stop-Process -Id $gui.Id -Force -EA SilentlyContinue
