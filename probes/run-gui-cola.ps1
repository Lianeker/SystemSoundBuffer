# ¿Se pierde el final al parar? Se graba, suena un pitido corto, se para
# INMEDIATAMENTE despues, se selecciona todo y se guarda. El pitido tiene que
# estar entero en el fichero, no cortado.
param([string]$Beep = 'C:\Windows\Media\Windows Ding.wav')
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')
if (-not (Test-Path $Beep)) { $Beep = 'C:\Windows\Media\notify.wav' }

$gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @(
    '--theme', 'dark', '--buffer', '0', '--src', "app:$PID")
Start-Sleep -Seconds 3

Add-Type -AssemblyName System.Windows.Forms
$src = @'
using System;
using System.Runtime.InteropServices;
public class W9 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
'@
if (-not ('W9' -as [type])) { Add-Type -TypeDefinition $src }
[void][W9]::SetForegroundWindow($gui.MainWindowHandle)
Start-Sleep -Milliseconds 600

Write-Output 'Grabar (Ctrl+P)'
[System.Windows.Forms.SendKeys]::SendWait('^p')
Start-Sleep -Seconds 2

Write-Output "pitido: $Beep (este proceso lo emite, y es la fuente grabada)"
$s = New-Object Media.SoundPlayer $Beep
$s.PlaySync()

Write-Output 'Parar INMEDIATAMENTE (Ctrl+P), seleccionar todo (Ctrl+A) y guardar (Ctrl+S)'
[void][W9]::SetForegroundWindow($gui.MainWindowHandle)
[System.Windows.Forms.SendKeys]::SendWait('^p')
Start-Sleep -Milliseconds 400
[System.Windows.Forms.SendKeys]::SendWait('^a')
Start-Sleep -Milliseconds 300
[System.Windows.Forms.SendKeys]::SendWait('^s')
Start-Sleep -Milliseconds 1500

Get-ChildItem ssb-gui-buffer\*.wav -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 3 |
    ForEach-Object { Write-Output ("  {0,-30} {1,9:N0} bytes" -f $_.Name, $_.Length) }

Stop-Process -Id $gui.Id -Force -ErrorAction SilentlyContinue
