# Repite el caso exacto que una vez dio "Saved 0 of 1 tracks": UNA sola pista de
# loopback por proceso, 45 s. Se ejecuta N veces para ver si es reproducible.
param([int]$Veces = 2, [double]$Secs = 45)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')

for ($k = 1; $k -le $Veces; $k++) {
    Remove-Item -Recurse -Force ssb-gui-buffer, up -EA SilentlyContinue
    New-Item -ItemType Directory -Force -Path up | Out-Null
    $dest = (Resolve-Path up).Path

    $wav = 'C:\Windows\Media\Alarm01.wav'
    $p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
        '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; `$p.PlayLooping(); Start-Sleep -Seconds $($Secs + 30)")
    Start-Sleep -Milliseconds 900

    # Sin BOM: se escribe en ASCII a proposito, aunque el cargador ya lo tolere.
    @(
        "add app:$($p1.Id)",
        "buffer 120",
        "folder $dest",
        "export wav",
        "wait 1",
        "rec",
        "wait $Secs",
        "stop",
        "wait 1",
        "all",
        "save",
        "tracks",
        "wait 3",
        "quit"
    ) | Set-Content -Path up.ssb -Encoding Ascii

    $gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--mode','cmd','--script','up.ssb')
    if (-not $gui.WaitForExit(($Secs + 40) * 1000)) { Stop-Process -Id $gui.Id -Force -EA SilentlyContinue }
    Stop-Process -Id $p1.Id -Force -EA SilentlyContinue

    $linea = (Get-Content up.ssb.log -EA SilentlyContinue | Select-String 'Saved').ToString()
    $f = Get-ChildItem "$dest\*.wav" -EA SilentlyContinue | Select-Object -First 1
    $tam = if ($f) { '{0:N0} bytes' -f $f.Length } else { 'ningun fichero' }
    Write-Output ("intento {0}: {1}   -> {2}" -f $k, $linea.Trim(), $tam)
}
