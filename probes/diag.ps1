# Diagnostico: ejecuta un guion corto y CAPTURA LA CONSOLA del programa.
param([string]$Guion = "add output`nbuffer 60`nexport wav`nwait 1`nrec`nwait 8`nstop`nwait 1`nall`nsave`ntracks")
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer, dg -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path dg | Out-Null
$dest = (Resolve-Path dg).Path

$wav = 'C:\Windows\Media\Alarm01.wav'
$player = {
    param($p)
    $s = New-Object Media.SoundPlayer $p
    $s.PlayLooping()
    Start-Sleep -Seconds 60
}
$job = Start-Job -ScriptBlock $player -ArgumentList $wav
Start-Sleep -Seconds 2

$lines = @()
foreach ($l in ($Guion -split "`n")) { $lines += $l }
$lines = $lines[0..2] + @("folder $dest") + $lines[3..($lines.Count-1)]
$lines | Set-Content dg.ssb -Encoding UTF8
Write-Output '--- guion ---'
$lines | ForEach-Object { Write-Output "   $_" }

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--mode','cmd','--script','dg.ssb')
Add-Type -AssemblyName System.Drawing
$sc = @'
using System; using System.Runtime.InteropServices;
public class WG {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h,int x,int y,int w,int t,bool p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; } }
'@
if (-not ('WG' -as [type])) { Add-Type -TypeDefinition $sc }
[void][WG]::SetProcessDPIAware()
Start-Sleep -Seconds 3
[void][WG]::MoveWindow($gui.MainWindowHandle, 20, 20, 1280, 700, $true)
Start-Sleep -Seconds 14
$r = New-Object WG+RECT
if ([WG]::GetWindowRect($gui.MainWindowHandle, [ref]$r)) {
    $b = New-Object System.Drawing.Bitmap ($r.R-$r.L), ($r.B-$r.T)
    $g = [System.Drawing.Graphics]::FromImage($b)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $b.Size); $g.Dispose()
    $b.Save((Join-Path (Get-Location) 'dg.png'), [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
    Write-Output 'captura: dg.png'
} else { Write-Output 'sin ventana (el programa ya no esta)' }

Stop-Process -Id $gui.Id -Force -EA SilentlyContinue
Stop-Job $job -EA SilentlyContinue; Remove-Job $job -Force -EA SilentlyContinue

Write-Output ''
Write-Output '=== ficheros ==='
Get-ChildItem dg -EA SilentlyContinue | ForEach-Object { Write-Output ("  {0}  {1:N0} bytes" -f $_.Name, $_.Length) }
