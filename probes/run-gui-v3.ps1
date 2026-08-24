# Comprueba: interfaz en ingles, boton de idioma, scroll con la rueda sobre la
# lista de pistas, y silenciar una pista pulsando su nombre.
param([double]$Secs = 14, [string]$Theme = 'dark')
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')

$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; 1..60 | ForEach-Object { `$p.PlaySync() }")
Start-Sleep -Milliseconds 700
$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @(
    '--theme', $Theme, '--buffer', '1', '--export', 'mp3',
    '--src', 'output', '--src', "app:$($p1.Id)", '--src', 'input', '--src', 'output')
Start-Sleep -Seconds $Secs

Add-Type -AssemblyName System.Drawing
$src = @'
using System;
using System.Runtime.InteropServices;
public class W7 {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int t, bool p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public const uint DOWN=0x0002, UP=0x0004, WHEEL=0x0800;
}
'@
if (-not ('W7' -as [type])) { Add-Type -TypeDefinition $src }
[void][W7]::SetProcessDPIAware()
[void][W7]::MoveWindow($gui.MainWindowHandle, 20, 20, 1300, 700, $true)
[void][W7]::SetForegroundWindow($gui.MainWindowHandle)
Start-Sleep -Milliseconds 1000
$r = New-Object W7+RECT
[void][W7]::GetWindowRect($gui.MainWindowHandle, [ref]$r)
$w = $r.R - $r.L; $h = $r.B - $r.T

function Shot($n) {
    $b = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($b)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $b.Size); $g.Dispose()
    $b.Save((Join-Path (Get-Location) $n), [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
}

Shot 'v3-en.png'
Write-Output 'v3-en.png : interfaz por omision (deberia estar en ingles)'

# Rueda hacia abajo sobre el lienzo: debe desplazar la lista de pistas.
[void][W7]::SetCursorPos($r.L + 600, $r.T + 400)
Start-Sleep -Milliseconds 300
for ($i=0; $i -lt 5; $i++) { [W7]::mouse_event([W7]::WHEEL, 0, 0, [uint32]4294967176, [IntPtr]::Zero); Start-Sleep -Milliseconds 120 }
Start-Sleep -Milliseconds 600
Shot 'v3-scroll.png'
Write-Output 'v3-scroll.png : tras cinco muescas de rueda hacia abajo'

# Pulsar el nombre de la primera pista: debe silenciarla.
[void][W7]::SetCursorPos($r.L + 120, $r.T + 222)
Start-Sleep -Milliseconds 200
[W7]::mouse_event([W7]::DOWN,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 90
[W7]::mouse_event([W7]::UP,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 700
Shot 'v3-mute.png'
Write-Output 'v3-mute.png : tras pulsar el nombre de una pista'

Stop-Process -Id $gui.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $p1.Id -Force -ErrorAction SilentlyContinue
