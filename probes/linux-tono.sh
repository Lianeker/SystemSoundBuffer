#!/bin/sh
# Comprueba que la captura en Linux graba DE VERDAD lo que suena.
#
#     sh probes/linux-tono.sh [segundos] [build]
#
# Levanta un servidor de sonido propio con un sink virtual, mete por el un tono
# de frecuencia conocida, graba con el motor y comprueba que el WAV resultante
# contiene ESE tono y no otra cosa.
#
# Existe porque hasta ahora el CI de Linux compilaba y enumeraba fuentes sin
# servidor de sonido. Con eso, "la captura funciona en Linux" no lo respaldaba
# ninguna ejecucion: la tabla del README lo prometia y nada lo comprobaba.
#
# No hace falta tarjeta de sonido ni pantalla, asi que sirve igual en un runner
# de integracion continua. El servidor se levanta en su propio directorio de
# ejecucion para no tocar la sesion de audio de quien lo ejecute.
SECS=${1:-10}
BUILD=${2:-build-linux}
FREQ=${FREQ:-440}
SINK=ssbtono

cd "$(dirname "$0")/.." || exit 1

if [ ! -x "$BUILD/ssb" ]; then
    echo "no encuentro $BUILD/ssb; compila primero o pasa el directorio como 2o argumento"
    exit 1
fi

for t in pulseaudio pactl python3; do
    command -v $t >/dev/null 2>&1 || { echo "falta $t"; exit 1; }
done

TRABAJO=$(mktemp -d /tmp/ssb-tono.XXXXXX)
PULSE_RUNTIME_PATH="$TRABAJO/pulse"
export PULSE_RUNTIME_PATH
unset PULSE_SERVER

limpia() {
    [ -n "$PA_PID" ] && kill "$PA_PID" 2>/dev/null
    rm -rf "$TRABAJO"
}
trap limpia EXIT INT TERM

# -n: sin el guion de arranque por omision. Todo lo que carga esta aqui escrito,
# asi que el servidor es el mismo en cualquier maquina.
pulseaudio --daemonize=no -n --exit-idle-time=-1 \
    --load="module-native-protocol-unix" \
    --load="module-null-sink sink_name=$SINK sink_properties=device.description=SSB_tono" \
    --log-target=file:"$TRABAJO/pulse.log" &
PA_PID=$!

i=0
while [ $i -lt 50 ]; do
    pactl info >/dev/null 2>&1 && break
    i=$((i + 1))
    sleep 0.2
done
if ! pactl info >/dev/null 2>&1; then
    echo "el servidor de sonido no arranco"
    sed -n '1,40p' "$TRABAJO/pulse.log" 2>/dev/null
    exit 1
fi

pactl set-default-sink "$SINK" >/dev/null 2>&1

echo "=== servidor ==="
pactl info | grep -E "Server Name|Server Version|Default Sink"

echo
echo "=== fuentes que ve el motor ==="
"./$BUILD/ssb" list

echo
echo "=== tono de $FREQ Hz por $SINK, grabando $SECS s ==="
MOD=$(pactl load-module module-sine sink="$SINK" frequency="$FREQ") || {
    echo "no se pudo cargar module-sine"; exit 1; }

# Un momento para que el tono este sonando antes de empezar a grabar: si se
# graba desde el primer instante, el principio del WAV es silencio y el centro
# que analiza tono.py podria caer en el.
sleep 1
"./$BUILD/ssb" rec --secs "$SECS" --buffer 60 --src output --out "$TRABAJO/buf"
pactl unload-module "$MOD" 2>/dev/null

WAV="$TRABAJO/buf/pista0.wav"
if [ ! -f "$WAV" ]; then
    echo "FALLO: no se genero $WAV"
    exit 1
fi

echo
echo "=== comprobacion de la senal ==="
python3 probes/tono.py "$WAV" "$FREQ"
RES=$?

echo
if [ $RES -eq 0 ]; then
    echo "captura en Linux: OK"
else
    echo "captura en Linux: FALLO"
fi
exit $RES
