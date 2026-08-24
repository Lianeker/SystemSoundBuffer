# Pausa y reanudacion. El hueco de la pausa es REAL y tiene que verse donde
# ocurrio; lo que no puede aparecer es ningun otro.
#
# Sin pulsaciones simuladas: todo por --script.
param([double]$A = 12, [double]$Pausa = 3, [double]$B = 12)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer, pau -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path pau | Out-Null
$dest = (Resolve-Path pau).Path

$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; `$p.PlayLooping(); Start-Sleep -Seconds 120")
Start-Sleep -Milliseconds 900

$script = Join-Path (Get-Location) 'pau.ssb'
@(
    "add output",
    "buffer 180",
    "folder $dest",
    "export wav",
    "wait 1",
    "rec",
    "wait $A",
    "stop",
    "wait $Pausa",
    "rec",
    "wait $B",
    "stop",
    "wait 1",
    "all",
    "save",
    "tracks",
    "wait 6",
    "quit"
) | Set-Content -Path $script -Encoding UTF8

$total = $A + $Pausa + $B + 20
$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--mode','cmd','--script',$script)
Write-Output "grabar $A s / pausa $Pausa s / grabar $B s"

Add-Type -AssemblyName System.Drawing
$sc = @'
using System; using System.Runtime.InteropServices;
public class WP {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h,int x,int y,int w,int t,bool p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; } }
'@
if (-not ('WP' -as [type])) { Add-Type -TypeDefinition $sc }
[void][WP]::SetProcessDPIAware()
Start-Sleep -Seconds 2
[void][WP]::MoveWindow($gui.MainWindowHandle,20,20,1280,640,$true)
Start-Sleep -Seconds ($A + $Pausa + $B + 4)
$r = New-Object WP+RECT
if ([WP]::GetWindowRect($gui.MainWindowHandle,[ref]$r)) {
    $b = New-Object System.Drawing.Bitmap ($r.R-$r.L),($r.B-$r.T)
    $g = [System.Drawing.Graphics]::FromImage($b)
    $g.CopyFromScreen($r.L,$r.T,0,0,$b.Size); $g.Dispose()
    $b.Save((Join-Path (Get-Location) 'pausa.png'),[System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
    Write-Output 'pausa.png'
}
if (-not $gui.WaitForExit($total * 1000)) {
    Stop-Process -Id $gui.Id -Force -EA SilentlyContinue
}
Stop-Process -Id $p1.Id -Force -EA SilentlyContinue

Write-Output ''
Write-Output '=== exportado ==='
$got = Get-ChildItem "$dest\*.wav" -EA SilentlyContinue
if ($got) { $got | ForEach-Object { Write-Output ("  {0,-34} {1,11:N0} bytes" -f $_.Name, $_.Length) } }
else { Write-Output '  NADA' }
