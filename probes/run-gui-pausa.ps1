# Dos comprobaciones sobre un buffer corto (10 s), que llega a regimen enseguida:
#   1. En regimen, la onda se desplaza a la par por los dos bordes, sin tirones.
#   2. Ctrl+P detiene la entrada y los buffers se quedan EXACTAMENTE como estan.
param([double]$Secs = 16, [int]$Shots = 6, [int]$GapMs = 400)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')

$wav = 'C:\Windows\Media\Alarm01.wav'
$player = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command',
    "`$p = New-Object Media.SoundPlayer '$wav'; 1..60 | ForEach-Object { `$p.PlaySync() }"
)
Start-Sleep -Milliseconds 800
# --buffer 0 = 10 s: en regimen en once segundos.
$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @(
    '--theme', 'dark', '--buffer', '0', '--src', 'output', '--src', "app:$($player.Id)")
Start-Sleep -Seconds $Secs

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$src = @'
using System;
using System.Runtime.InteropServices;
public class W6 {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
'@
if (-not ('W6' -as [type])) { Add-Type -TypeDefinition $src }
[void][W6]::SetProcessDPIAware()
[void][W6]::SetForegroundWindow($gui.MainWindowHandle)
Start-Sleep -Milliseconds 800
$r = New-Object W6+RECT
[void][W6]::GetWindowRect($gui.MainWindowHandle, [ref]$r)
$w = $r.R - $r.L; $h = $r.B - $r.T

function Shot($name) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
    $g.Dispose()
    $bmp.Save((Join-Path (Get-Location) $name), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

Write-Output "--- en regimen: $Shots capturas cada $GapMs ms ---"
for ($i = 0; $i -lt $Shots; $i++) { Shot "mov-$i.png"; Start-Sleep -Milliseconds $GapMs }

Write-Output "--- Ctrl+E: congelar la vista (la captura sigue) ---"
[System.Windows.Forms.SendKeys]::SendWait('^e')
Start-Sleep -Milliseconds 900
Shot "vista-0.png"
Start-Sleep -Milliseconds 1600
Shot "vista-1.png"
[System.Windows.Forms.SendKeys]::SendWait('^e')
Start-Sleep -Milliseconds 700

Write-Output "--- Ctrl+P: detener la entrada ---"
[System.Windows.Forms.SendKeys]::SendWait('^p')
Start-Sleep -Milliseconds 900
Shot "pausa-0.png"
Start-Sleep -Seconds 3
Shot "pausa-1.png"
Write-Output "dos capturas separadas 3 s con la entrada detenida"

Stop-Process -Id $gui.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $player.Id -Force -ErrorAction SilentlyContinue
