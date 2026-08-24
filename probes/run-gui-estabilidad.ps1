# Comprueba que la parte YA GRABADA de la onda no se mueve entre repintados.
# Toma varias capturas seguidas y las deja para comparar: lo viejo debe ser
# identico pixel a pixel, y solo debe cambiar el borde donde se sigue grabando.
param([double]$Secs = 18, [int]$Shots = 3, [int]$GapMs = 500, [string]$Theme = 'dark')
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')

$wav = 'C:\Windows\Media\Alarm01.wav'
$player = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command',
    "`$p = New-Object Media.SoundPlayer '$wav'; 1..40 | ForEach-Object { `$p.PlaySync() }"
)
Start-Sleep -Milliseconds 800
$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @(
    '--theme', $Theme, '--buffer', '2', '--src', 'output', '--src', "app:$($player.Id)")
Start-Sleep -Seconds $Secs

Add-Type -AssemblyName System.Drawing
$src = @'
using System;
using System.Runtime.InteropServices;
public class W5 {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
'@
if (-not ('W5' -as [type])) { Add-Type -TypeDefinition $src }
[void][W5]::SetProcessDPIAware()
[void][W5]::SetForegroundWindow($gui.MainWindowHandle)
Start-Sleep -Milliseconds 800

$r = New-Object W5+RECT
[void][W5]::GetWindowRect($gui.MainWindowHandle, [ref]$r)
$w = $r.R - $r.L; $h = $r.B - $r.T
for ($i = 0; $i -lt $Shots; $i++) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
    $g.Dispose()
    $bmp.Save((Join-Path (Get-Location) "estab-$i.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Start-Sleep -Milliseconds $GapMs
}
Write-Output "$Shots capturas de ${w}x${h}, separadas $GapMs ms"

Stop-Process -Id $gui.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $player.Id -Force -ErrorAction SilentlyContinue
