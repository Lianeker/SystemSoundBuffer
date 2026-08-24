# Arranca la interfaz con tres pistas ya puestas, deja que grabe unos segundos
# con audio sonando, y captura la ventana a PNG para poder mirarla.
param([double]$Secs = 12, [string]$Png = 'gui.png')
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')

$wav = 'C:\Windows\Media\Alarm01.wav'
$player = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command',
    "`$p = New-Object Media.SoundPlayer '$wav'; 1..40 | ForEach-Object { `$p.PlaySync() }"
)
Start-Sleep -Milliseconds 800

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @(
    '--src', 'output',
    '--src', "app:$($player.Id)",
    '--src', 'input'
)
Write-Output "ssbgui PID $($gui.Id), emisor PID $($player.Id)"
Start-Sleep -Seconds $Secs

$gui.Refresh()
if ($gui.HasExited) {
    Write-Output "LA INTERFAZ SE CERRO SOLA (codigo $($gui.ExitCode))"
    Stop-Process -Id $player.Id -Force -ErrorAction SilentlyContinue
    exit 1
}
Write-Output "sigue viva tras $Secs s; ventana: '$($gui.MainWindowTitle)'"

Add-Type -AssemblyName System.Drawing
$src = @'
using System;
using System.Runtime.InteropServices;
public class Win {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int t, bool repaint);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
'@
if (-not ('Win' -as [type])) { Add-Type -TypeDefinition $src }

# Redimensionar antes de capturar prueba de paso que el layout se recoloca.
[void][Win]::MoveWindow($gui.MainWindowHandle, 20, 20, 1440, 800, $true)
[void][Win]::SetForegroundWindow($gui.MainWindowHandle)
Start-Sleep -Milliseconds 1200
$r = New-Object Win+RECT
[void][Win]::GetWindowRect($gui.MainWindowHandle, [ref]$r)
$w = $r.R - $r.L; $h = $r.B - $r.T
Write-Output "ventana de $w x $h en ($($r.L),$($r.T))"
if ($w -gt 0 -and $h -gt 0) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
    $g.Dispose()
    $bmp.Save((Join-Path (Get-Location) $Png), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output "captura en $Png"
}

Stop-Process -Id $gui.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $player.Id -Force -ErrorAction SilentlyContinue
