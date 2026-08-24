# Una fuente que desaparece mientras se graba.
#
# Antes esto congelaba el programa: la interfaz preguntaba al sistema por el
# estado del dispositivo una vez por segundo y por pista, y esas llamadas COM se
# atascan justo cuando algo esta desapareciendo.
#
# Lo que hay que demostrar:
#   - que el programa SIGUE VIVO y respondiendo (el guion avanza)
#   - que se entera de que la fuente ya no esta
#   - que la lista de fuentes se refresca sola
param([double]$Antes = 8, [double]$Despues = 10)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
Remove-Item -Recurse -Force ssb-gui-buffer -EA SilentlyContinue

$wav = 'C:\Windows\Media\Alarm01.wav'
$p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; `$p.PlayLooping(); Start-Sleep -Seconds 300")
Start-Sleep -Milliseconds 900
Write-Output "fuente: powershell pid $($p1.Id)"

@(
    "add output",
    "add app:$($p1.Id)",
    "buffer 60",
    "wait 1",
    "rec",
    "wait $Antes",
    "tracks",
    "MATAR",
    "wait $Despues",
    "tracks",
    "list",
    "wait 2",
    "stop",
    "quit"
) | Set-Content -Path muere.ssb -Encoding Ascii

# El guion no puede matar el proceso: lo hace este script cuando toca. Se
# sustituye la linea MATAR por una espera y se mata desde fuera en ese momento.
(Get-Content muere.ssb) -replace '^MATAR$', 'wait 1' | Set-Content muere.ssb -Encoding Ascii

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--mode','cmd','--script','muere.ssb')
Start-Sleep -Seconds (3 + $Antes + 1)

Write-Output "matando la fuente..."
Stop-Process -Id $p1.Id -Force -EA SilentlyContinue

# Prueba de vida: si la ventana no responde a un mensaje en 5 s, esta colgada.
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public class LV {
  [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Auto)]
  public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam, uint flags, uint timeout, out IntPtr result);
}
'@
for ($i = 1; $i -le 6; $i++) {
    Start-Sleep -Seconds 2
    $r = [IntPtr]::Zero
    $ok = [LV]::SendMessageTimeout($gui.MainWindowHandle, 0x0000, [IntPtr]::Zero, [IntPtr]::Zero, 0x0002, 3000, [ref]$r)
    Write-Output ("  +{0}s  responde: {1}" -f (2*$i), ($ok -ne [IntPtr]::Zero))
}

if (-not $gui.WaitForExit(40000)) {
    Write-Output 'el programa no termino solo; se cierra'
    Stop-Process -Id $gui.Id -Force -EA SilentlyContinue
}

Write-Output ''
Write-Output '=== registro ==='
Get-Content muere.ssb.log -EA SilentlyContinue | ForEach-Object { Write-Output "  $_" }
