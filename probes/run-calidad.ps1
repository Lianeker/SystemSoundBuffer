# Graba lo mismo a 16 y a 24 bits y compara: profundidad del WAV, contenido y
# cuanto ocupa el buffer. Lo que hay que demostrar es que 24 bits NO desactiva
# la compresion, porque de eso depende que un buffer de horas siga cabiendo.
param([double]$Secs = 20)
$ErrorActionPreference = 'Continue'
Set-Location (Join-Path $PSScriptRoot '..\build-dbg')

$wav = 'C:\Windows\Media\Alarm01.wav'

foreach ($bits in 16, 24) {
    Remove-Item -Recurse -Force ssb-gui-buffer, "q$bits" -EA SilentlyContinue
    New-Item -ItemType Directory -Force -Path "q$bits" | Out-Null
    $dest = (Resolve-Path "q$bits").Path

    $p1 = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
        '-NoProfile', '-Command', "`$p = New-Object Media.SoundPlayer '$wav'; `$p.PlayLooping(); Start-Sleep -Seconds $($Secs + 30)")
    Start-Sleep -Milliseconds 900

    @(
        "quality $bits",
        "add output",
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
    ) | Set-Content -Path "q.ssb" -Encoding Ascii

    $gui = Start-Process .\ssbgui.exe -PassThru -ArgumentList @('--mode','cmd','--script','q.ssb')
    if (-not $gui.WaitForExit(($Secs + 40) * 1000)) { Stop-Process -Id $gui.Id -Force -EA SilentlyContinue }
    Stop-Process -Id $p1.Id -Force -EA SilentlyContinue

    Write-Output "--- $bits bits ---"
    Get-Content q.ssb.log -EA SilentlyContinue | Select-String 'resolucion|Saved|MB' | ForEach-Object { Write-Output "   $_" }
    Get-ChildItem "$dest\*.wav" -EA SilentlyContinue | ForEach-Object {
        Write-Output ("   {0}  {1:N0} bytes" -f $_.Name, $_.Length)
    }
}
