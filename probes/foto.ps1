# Ejecuta un guion en la aplicacion y FOTOGRAFIA la ventana.
#
# Con PrintWindow, que le pide a la ventana que se dibuje: otra aplicacion
# encima no puede falsear la imagen. CopyFromScreen ya nos hizo fotografiar una
# conversacion ajena que se habia puesto delante (docs/12).
#
#     foto.ps1 -Lineas 'add output; wait 2' -Salida antes.png -Espera 6
#
# Las ordenes van en UNA cadena separadas por ';'. Pasar un array desde otra
# shell se convierte en una sola cadena con comas y el guion falla sin decir por
# que; con un separador explicito no hay forma de equivocarse.
param(
    [string]$Lineas = 'add output',
    [string]$Salida = 'foto.png',
    [double]$Espera = 5,
    [int]$Ancho = 1280,
    [int]$Alto = 560,
    [string]$Teclas = '',
    [string]$Raton = '',
    [int]$RatonBoton = -1,
    [string]$Extra = '',
    [switch]$ConSonido
)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer -EA SilentlyContinue

$p1 = $null
if ($ConSonido) {
    $wav = 'C:\Windows\Media\Alarm01.wav'
    $p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
        '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; `$p.PlayLooping(); Start-Sleep -Seconds 120")
    Start-Sleep -Milliseconds 900
}

# @(...) a la fuerza: con UNA sola orden, -split devuelve una cadena y no un
# array, y el `+` la concatena en vez de anadir elementos. El guion salia en una
# sola linea y no se parseaba nada.
$ordenes = @($Lineas -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
($ordenes + 'wait 120' + 'quit') | Set-Content -Path foto.ssb -Encoding Ascii
$argumentos = @('--mode','cmd','--script','foto.ssb')
if ($Extra -ne '') { $argumentos += @($Extra -split ' ' | Where-Object { $_ -ne '' }) }
$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList $argumentos

Add-Type -AssemblyName System.Drawing
$src = @'
using System;
using System.Runtime.InteropServices;
public class FT {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h,int x,int y,int w,int t,bool p);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT p);
    [StructLayout(LayoutKind.Sequential)] public struct PT { public int X,Y; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
'@
if (-not ('FT' -as [type])) { Add-Type -TypeDefinition $src }
[void][FT]::SetProcessDPIAware()
Start-Sleep -Seconds 3
[void][FT]::MoveWindow($gui.MainWindowHandle, 20, 20, $Ancho, $Alto, $true)
Start-Sleep -Seconds $Espera
if ($Teclas -ne '') {
    Add-Type -AssemblyName System.Windows.Forms
    [void][FT]::SetForegroundWindow($gui.MainWindowHandle)
    Start-Sleep -Milliseconds 600
    [System.Windows.Forms.SendKeys]::SendWait($Teclas)
    Start-Sleep -Seconds 2
}

# El resaltado de un boton plano lo decide Windows mirando donde esta el cursor
# de VERDAD, asi que para fotografiarlo hay que mover el raton. Coordenadas de
# pantalla, porque la ventana se acaba de colocar en 20,20. Se devuelve a su
# sitio al terminar: mover el raton de alguien y dejarselo movido es de mala
# educacion, y ademas falsea la siguiente foto.
# Apuntar a un boton POR ORDEN, no por coordenadas. Las coordenadas hay que
# sacarlas de una foto anterior y dejan de valer en cuanto la barra se recoloca
# — que es justo lo que pasa al cambiar de tamano la ventana. El orden de los
# botones, no.
if ($RatonBoton -ge 0) {
    $botones = New-Object System.Collections.ArrayList
    $cb = [FT+EnumProc]{
        param($h, $l)
        $c = New-Object Text.StringBuilder 64
        [void][FT]::GetClassName($h, $c, 64)
        if ($c.ToString() -eq 'Button') {
            $rr = New-Object FT+RECT
            [void][FT]::GetWindowRect($h, [ref]$rr)
            [void]$botones.Add([pscustomobject]@{ T = $rr.T; L = $rr.L; X = [int](($rr.L + $rr.R) / 2); Y = [int](($rr.T + $rr.B) / 2) })
        }
        return $true
    }
    [void][FT]::EnumChildWindows($gui.MainWindowHandle, $cb, [IntPtr]::Zero)
    $ord = @($botones | Sort-Object T, L)
    Write-Output "  botones encontrados: $($ord.Count)"
    if ($RatonBoton -lt $ord.Count) {
        $Raton = "$($ord[$RatonBoton].X),$($ord[$RatonBoton].Y)"
        Write-Output "  apuntando al boton $RatonBoton en $Raton"
    }
}

$guardado = New-Object FT+PT
if ($Raton -ne '') {
    # Y la ventana TIENE que estar delante: el resaltado se decide con
    # WindowFromPoint, que devuelve la de encima. Con otra ventana tapando, el
    # raton esta sobre esa y el boton no se entera. Nos costo una foto sin
    # resaltado y un rato buscandolo en el codigo del SDK.
    [void][FT]::SetForegroundWindow($gui.MainWindowHandle)
    Start-Sleep -Milliseconds 400
    [void][FT]::GetCursorPos([ref]$guardado)
    $xy = $Raton -split ','
    [void][FT]::SetCursorPos([int]$xy[0], [int]$xy[1])
    Start-Sleep -Milliseconds 700
}

$r = New-Object FT+RECT
[void][FT]::GetWindowRect($gui.MainWindowHandle, [ref]$r)
$b = New-Object System.Drawing.Bitmap ($r.R-$r.L), ($r.B-$r.T)
$g = [System.Drawing.Graphics]::FromImage($b)
$hdc = $g.GetHdc()
[void][FT]::PrintWindow($gui.MainWindowHandle, $hdc, 2)   # 2 = PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc); $g.Dispose()
$b.Save((Join-Path (Get-Location) $Salida), [System.Drawing.Imaging.ImageFormat]::Png)
$b.Dispose()

if ($Raton -ne '') { [void][FT]::SetCursorPos($guardado.X, $guardado.Y) }
Stop-Process -Id $gui.Id -Force -EA SilentlyContinue
if ($p1 -ne $null) { Stop-Process -Id $p1.Id -Force -EA SilentlyContinue }
Write-Output "guardada $Salida"
Get-Content foto.ssb.log -EA SilentlyContinue | Select-Object -Last 4 | ForEach-Object { Write-Output "  $_" }
