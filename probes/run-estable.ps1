# ¿Cambia la forma de la onda mientras la grafica se traslada?
#
# Se graba, se toman DOS capturas separadas en el tiempo y se compara el
# contorno de la onda alineandolo por el desplazamiento. Si la forma es estable,
# al alinear las dos capturas el contorno coincide; si "baila", no.
#
# Las capturas van por PrintWindow, no por CopyFromScreen: PrintWindow pide a la
# ventana que se dibuje, asi que otra aplicacion encima no puede falsear la
# medida. Ya nos paso.
param([double]$Secs = 45, [double]$Gap = 1.5)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer, est -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path est | Out-Null

$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; `$p.PlayLooping(); Start-Sleep -Seconds $($Secs + 40)")
Start-Sleep -Milliseconds 900

@(
    "quality 16",
    "add output",
    "buffer 20",
    "wait 1",
    "rec",
    "wait $Secs",
    "wait 60",
    "quit"
) | Set-Content -Path est.ssb -Encoding Ascii

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--theme','dark','--mode','cmd','--script','est.ssb')

Add-Type -AssemblyName System.Drawing
$src = @'
using System;
using System.Runtime.InteropServices;
public class PW {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h,int x,int y,int w,int t,bool p);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
'@
if (-not ('PW' -as [type])) { Add-Type -TypeDefinition $src }
[void][PW]::SetProcessDPIAware()
Start-Sleep -Seconds 3
[void][PW]::MoveWindow($gui.MainWindowHandle, 20, 20, 1280, 560, $true)
Start-Sleep -Seconds ($Secs + 2)

function Shot($name) {
    $r = New-Object PW+RECT
    [void][PW]::GetWindowRect($gui.MainWindowHandle, [ref]$r)
    $b = New-Object System.Drawing.Bitmap ($r.R-$r.L), ($r.B-$r.T)
    $g = [System.Drawing.Graphics]::FromImage($b)
    $hdc = $g.GetHdc()
    # 2 = PW_RENDERFULLCONTENT: necesario para ventanas que pintan por DirectX
    [void][PW]::PrintWindow($gui.MainWindowHandle, $hdc, 2)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $b.Save((Join-Path (Get-Location) "est\$name"), [System.Drawing.Imaging.ImageFormat]::Png)
    $b.Dispose()
}

Shot 'a.png'
Start-Sleep -Seconds $Gap
Shot 'b.png'

Stop-Process -Id $gui.Id -Force -EA SilentlyContinue
Stop-Process -Id $p1.Id -Force -EA SilentlyContinue
Write-Output "capturas en est\a.png y est\b.png, separadas $Gap s"
