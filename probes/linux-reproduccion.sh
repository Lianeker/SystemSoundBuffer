#!/bin/sh
# Comprueba la reproduccion en Linux, escuchandola.
#
#     sh probes/linux-reproduccion.sh
#
# Que el programa diga "reproduciendo" no demuestra nada: lo demuestra que salga
# sonido. Asi que mientras la interfaz reproduce el tramo guardado, el motor
# graba el monitor del sink y se mira el pico. Si la reproduccion no suena, el
# pico sale a cero.
BUILD=${BUILD:-build-linux}
WAV=${WAV:-/tmp/Alarm01.wav}

if [ ! -f "$WAV" ]; then
    cp /mnt/c/Windows/Media/Alarm01.wav "$WAV" 2>/dev/null
fi
if [ ! -f "$WAV" ]; then
    echo "no encuentro un WAV para reproducir; pasa WAV=/ruta/al.wav"
    exit 1
fi
if [ -z "$PULSE_SERVER" ] && [ -S /mnt/wslg/PulseServer ]; then
    PULSE_SERVER=unix:/mnt/wslg/PulseServer
    export PULSE_SERVER
fi

rm -rf /tmp/ssb-repro
mkdir -p /tmp/ssb-repro
cd "$(dirname "$0")/.." || exit 1

cat > /tmp/repro.ssb <<'GUION'
add output
folder /tmp/ssb-repro
export wav
mix off
rec
wait 6
stop
wait 1
all
save
wait 1
play
wait 12
hush
quit
GUION

# Ruido durante la grabacion, para tener algo que reproducir despues.
( paplay "$WAV"; paplay "$WAV" ) &
RUIDO=$!

"./$BUILD/ssbgui" --lang en --mode cmd --script /tmp/repro.ssb &
GUI=$!

# La interfaz graba 6 s, guarda, y arranca la reproduccion sobre t=9. Se le da
# margen y se graba el monitor MIENTRAS suena.
sleep 11
kill $RUIDO 2>/dev/null
pkill pacat 2>/dev/null
sleep 1
echo "=== el monitor del sink mientras la interfaz reproduce ==="
"./$BUILD/ssb" rec --secs 5 --src output 2>&1 | grep -E "pico|capturados"

wait $GUI 2>/dev/null
echo
echo "=== lo que dijo la interfaz ==="
grep -E "reproduc|Saved|guardad|error|no se pudo|falla" /tmp/repro.ssb.log 2>/dev/null
echo
echo "=== fichero exportado ==="
ls -la /tmp/ssb-repro/ 2>/dev/null | tail -3
exit 0
