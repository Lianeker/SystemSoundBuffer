# Captura la interfaz en los dos temas y con el buffer a medio llenar, para
# comprobar que la onda crece hasta el tope y que lo que falta se ve como tal.
param([double]$Secs = 20, [string]$Theme = 'system', [string]$Png = 'tema.png', [int]$Buffer = 0, [switch]$NoResize)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')

$wav = 'C:\Windows\Media\Alarm01.wav'
$player = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command',
    "`$p = New-Object Media.SoundPlayer '$wav'; 1..40 | ForEach-Object { `$p.PlaySync() }"
)
Start-Sleep -Milliseconds 800
$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @(
    '--theme', $Theme, '--buffer', "$Buffer",
    '--src', 'output', '--src', "app:$($player.Id)")
Start-Sleep -Seconds $Secs

Add-Type -AssemblyName System.Drawing
$src = @'
using System;
using System.Runtime.InteropServices;
public class W3 {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int t, bool p);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
'@
if (-not ('W3' -as [type])) { Add-Type -TypeDefinition $src }
# Sin esto, GetWindowRect devuelve coordenadas virtualizadas y la captura recorta.
[void][W3]::SetProcessDPIAware()

if (-not $NoResize) { [void][W3]::MoveWindow($gui.MainWindowHandle, 20, 20, 1400, 620, $true) }
[void][W3]::SetForegroundWindow($gui.MainWindowHandle)
Start-Sleep -Milliseconds 1200
$r = New-Object W3+RECT
[void][W3]::GetWindowRect($gui.MainWindowHandle, [ref]$r)
$w = $r.R - $r.L; $h = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$g.Dispose()
$bmp.Save((Join-Path (Get-Location) $Png), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "tema=$Theme buffer=$Buffer -> $Png"

Stop-Process -Id $gui.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $player.Id -Force -ErrorAction SilentlyContinue
