# La carpeta elegida tiene que sobrevivir a cerrar el programa.
# Prueba de punta a punta: elegir carpeta -> cerrar -> volver a abrir -> grabar
# -> guardar, y comprobar que el fichero cae donde se dijo, sin volver a elegir.
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer, elegida -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path elegida | Out-Null
$dest = (Resolve-Path elegida).Path
$cfg  = Join-Path $env:APPDATA 'SystemSoundBuffer\ssb.cfg'
Remove-Item -Force $cfg -EA SilentlyContinue

Add-Type -AssemblyName System.Windows.Forms
$src = @'
using System;
using System.Runtime.InteropServices;
public class WC {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int t, bool p);
}
'@
if (-not ('WC' -as [type])) { Add-Type -TypeDefinition $src }
[void][WC]::SetProcessDPIAware()

$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; 1..80 | ForEach-Object { `$p.PlaySync() }")
Start-Sleep -Milliseconds 700

function Launch {
    $g = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--mode','cmd')
    Start-Sleep -Seconds 3
    [void][WC]::MoveWindow($g.MainWindowHandle, 20, 20, 1280, 620, $true)
    Start-Sleep -Milliseconds 700
    return $g
}
function Cmd($g, $t) {
    [void][WC]::SetForegroundWindow($g.MainWindowHandle)
    Start-Sleep -Milliseconds 700
    [void][WC]::SetForegroundWindow($g.MainWindowHandle)
    Start-Sleep -Milliseconds 300
    [System.Windows.Forms.SendKeys]::SendWait("$t{ENTER}")
    Start-Sleep -Milliseconds 1100
}

Write-Output '--- primera ejecucion: se elige la carpeta y se cierra ---'
$g1 = Launch
Cmd $g1 "folder $dest"
Cmd $g1 'export mp3'
Cmd $g1 'buffer 45'
Stop-Process -Id $g1.Id -Force -EA SilentlyContinue
Start-Sleep -Seconds 2

Write-Output ''
Write-Output "=== $cfg ==="
if (Test-Path $cfg) { Get-Content $cfg | ForEach-Object { Write-Output "  $_" } }
else { Write-Output '  NO EXISTE' }

Write-Output ''
Write-Output '--- segunda ejecucion: NO se toca la carpeta, solo se graba y guarda ---'
$g2 = Launch
Cmd $g2 'add output'
Cmd $g2 'rec'
Start-Sleep -Seconds 8
Cmd $g2 'stop'
Cmd $g2 'all'
Cmd $g2 'save'
Start-Sleep -Seconds 3
Add-Type -AssemblyName System.Drawing
$src2 = @'
using System;
using System.Runtime.InteropServices;
public class WR {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
'@
if (-not ('WR' -as [type])) { Add-Type -TypeDefinition $src2 }
$rr = New-Object WR+RECT
[void][WR]::GetWindowRect($g2.MainWindowHandle, [ref]$rr)
$bb = New-Object System.Drawing.Bitmap ($rr.R-$rr.L), ($rr.B-$rr.T)
$gg = [System.Drawing.Graphics]::FromImage($bb)
$gg.CopyFromScreen($rr.L, $rr.T, 0, 0, $bb.Size); $gg.Dispose()
$bb.Save((Join-Path (Get-Location) 'carpeta.png'), [System.Drawing.Imaging.ImageFormat]::Png); $bb.Dispose()
Write-Output 'carpeta.png'
Stop-Process -Id $g2.Id -Force -EA SilentlyContinue
Stop-Process -Id $p1.Id -Force -EA SilentlyContinue

Write-Output ''
Write-Output '=== en la carpeta elegida (sin haberla vuelto a elegir) ==='
$got = Get-ChildItem $dest -EA SilentlyContinue
if ($got) { $got | ForEach-Object { Write-Output ("  {0,-34} {1,11:N0} bytes" -f $_.Name, $_.Length) } }
else { Write-Output '  VACIA — la carpeta no se recordo' }
