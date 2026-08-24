#!/bin/sh
# Comprueba la captura en Linux: enumeracion, monitor del sink y por aplicacion.
#
# Se ejecuta dentro de WSL o de cualquier Linux con PulseAudio:
#
#     sh probes/linux-captura.sh [segundos]
#
# Reproduce un WAV en bucle mientras graba, para que haya senal real que medir.
# Sin eso la captura sale a cero y la prueba no dice nada.
SECS=${1:-25}
BUILD=${BUILD:-build-linux}
WAV=${WAV:-/tmp/Alarm01.wav}

if [ ! -f "$WAV" ]; then
    # En WSL vale cualquiera de los de Windows.
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

echo "=== servidor ==="
pactl info 2>&1 | grep -E "Server String|Server Name" || echo "  sin pactl"

echo
echo "=== fuentes que ve el motor ==="
"./$BUILD/ssb" list

# Un solo proceso de reproduccion, largo. Con varios `paplay` seguidos, cada uno
# es un sink-input distinto y la captura por aplicacion pierde su objetivo al
# terminar el primero.
REPS=$(( SECS / 8 + 2 ))
i=0
while [ $i -lt $REPS ]; do
    cat "$WAV"
    i=$(( i + 1 ))
done | paplay --raw --rate=22050 --channels=2 --format=s16le &
PLAYER=$!
sleep 1

echo
echo "=== sink-input en curso ==="
pactl list short sink-inputs

echo
echo "=== captura: monitor del sink y la aplicacion ==="
"./$BUILD/ssb" rec --secs "$SECS" --src output --src app:pacat 2>&1 | \
    grep -E "^  \[|ch a |discontinu|frecuencia|pico|capturados|ventana"

kill $PLAYER 2>/dev/null
pkill pacat 2>/dev/null
exit 0
