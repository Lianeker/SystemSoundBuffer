# Reproduce el escenario reportado: varias pistas, buffer corto, grabar, parar,
# seleccionar todo y guardar. Captura la ventana en cada paso.
param([string]$Buffer = '0', [double]$RecSecs = 12)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')

$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; 1..60 | ForEach-Object { `$p.PlaySync() }")
Start-Sleep -Milliseconds 700

New-Item -ItemType Directory -Force -Path repro | Out-Null
Get-ChildItem repro -Filter *.wav -ErrorAction SilentlyContinue | Remove-Item -Force

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @(
    '--theme', 'dark', '--buffer', $Buffer,
    '--src', 'output', '--src', "app:$($p1.Id)", '--src', 'input')
Start-Sleep -Seconds 3

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$src = @'
using System;
using System.Runtime.InteropServices;
public class WR {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int t, bool p);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
'@
if (-not ('WR' -as [type])) { Add-Type -TypeDefinition $src }
[void][WR]::SetProcessDPIAware()
[void][WR]::MoveWindow($gui.MainWindowHandle, 20, 20, 1320, 700, $true)
[void][WR]::SetForegroundWindow($gui.MainWindowHandle)
Start-Sleep -Milliseconds 900
$r = New-Object WR+RECT
[void][WR]::GetWindowRect($gui.MainWindowHandle, [ref]$r)
$w = $r.R - $r.L; $h = $r.B - $r.T
function Shot($n) {
    $b = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($b)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $b.Size); $g.Dispose()
    $b.Save((Join-Path (Get-Location) $n), [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
}

[void][WR]::SetForegroundWindow($gui.MainWindowHandle); Start-Sleep -Milliseconds 300
[System.Windows.Forms.SendKeys]::SendWait('^p')
Start-Sleep -Seconds $RecSecs
Shot 'rp-grabando.png'
Write-Output "rp-grabando.png : grabando con buffer indice $Buffer"

[void][WR]::SetForegroundWindow($gui.MainWindowHandle); Start-Sleep -Milliseconds 300
[System.Windows.Forms.SendKeys]::SendWait('^p')
Start-Sleep -Milliseconds 700
Shot 'rp-parado.png'
Write-Output 'rp-parado.png : parado'

[void][WR]::SetForegroundWindow($gui.MainWindowHandle); Start-Sleep -Milliseconds 300
[System.Windows.Forms.SendKeys]::SendWait('^a')
Start-Sleep -Milliseconds 500
Shot 'rp-selec.png'
[void][WR]::SetForegroundWindow($gui.MainWindowHandle); Start-Sleep -Milliseconds 300
[System.Windows.Forms.SendKeys]::SendWait('^s')
Start-Sleep -Milliseconds 2000
Shot 'rp-guardado.png'
Write-Output 'rp-guardado.png : tras seleccionar todo y guardar'

Write-Output ''
Write-Output '=== ficheros ==='
Get-ChildItem ssb-gui-buffer\*.wav -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 6 |
    ForEach-Object { Write-Output ("  {0,-32} {1,9:N0} bytes" -f $_.Name, $_.Length) }

Stop-Process -Id $gui.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $p1.Id -Force -ErrorAction SilentlyContinue
