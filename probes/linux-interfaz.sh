#!/bin/sh
# El ciclo completo de la interfaz en Linux, sin pantalla y sin tarjeta.
#
#     sh probes/linux-interfaz.sh [build]
#
# Levanta el mismo sink virtual con tono que `linux-tono.sh`, arranca la
# interfaz GTK bajo Xvfb, y le da por guion el ciclo entero: anadir la fuente,
# grabar, seleccionar un tramo y exportarlo. Al final comprueba que el WAV que
# escribio la interfaz contiene el tono.
#
# La diferencia con `linux-arranque.sh`, que solo abre y cierra: aqui un fallo
# en cualquier eslabon —la ventana, la captura, la seleccion, la exportacion en
# su hilo propio— sale como fallo. Abrir y cerrar no prueba que grabe.
BUILD=${1:-build-linux}
FREQ=${FREQ:-440}
SINK=ssbtono

RAIZ=$(cd "$(dirname "$0")/.." && pwd) || exit 1
cd "$RAIZ" || exit 1

if [ ! -x "$BUILD/ssbgui" ]; then
    echo "no encuentro $BUILD/ssbgui; compila con el SDK o pasa el directorio como argumento"
    exit 1
fi
for t in pulseaudio pactl xvfb-run python3; do
    command -v $t >/dev/null 2>&1 || { echo "falta $t"; exit 1; }
done

TRABAJO=$(mktemp -d /tmp/ssb-interfaz.XXXXXX)
PULSE_RUNTIME_PATH="$TRABAJO/pulse"
export PULSE_RUNTIME_PATH
unset PULSE_SERVER

limpia() {
    [ -n "$PA_PID" ] && kill "$PA_PID" 2>/dev/null
    rm -rf "$TRABAJO"
}
trap limpia EXIT INT TERM

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
pactl info >/dev/null 2>&1 || { echo "el servidor de sonido no arranco"; exit 1; }
pactl set-default-sink "$SINK" >/dev/null 2>&1
MOD=$(pactl load-module module-sine sink="$SINK" frequency="$FREQ") || {
    echo "no se pudo cargar module-sine"; exit 1; }

SALIDA="$TRABAJO/salida"
mkdir -p "$SALIDA"

# `save` trabaja en un hilo propio desde docs/26, asi que hay que esperarlo
# antes de cerrar; si no, se sale con la exportacion a medias.
cat > "$TRABAJO/ciclo.ssb" <<GUION
folder $SALIDA
buffer 30
add output
rec
wait 8
save 5
wait 5
tracks
quit
GUION

echo "=== la interfaz, bajo Xvfb ==="
cd "$TRABAJO" || exit 1
xvfb-run -a "$RAIZ/$BUILD/ssbgui" --lang en --script "$TRABAJO/ciclo.ssb"
RESGUI=$?
cd "$RAIZ" || exit 1
pactl unload-module "$MOD" 2>/dev/null

echo "codigo de salida de la interfaz: $RESGUI"
echo
echo "=== lo que hizo ==="
if [ -f "$TRABAJO/ciclo.ssb.log" ]; then
    cat "$TRABAJO/ciclo.ssb.log"
else
    echo "(no hay registro: la interfaz no llego a leer el guion)"
fi

WAV=$(ls -1 "$SALIDA"/*.wav 2>/dev/null | head -1)
echo
if [ -z "$WAV" ]; then
    echo "FALLO: la interfaz no exporto ningun WAV a $SALIDA"
    exit 1
fi

echo "=== comprobacion de lo que exporto la interfaz ==="
python3 "$RAIZ/probes/tono.py" "$WAV" "$FREQ"
RES=$?

echo
if [ $RESGUI -ne 0 ]; then
    echo "interfaz en Linux: FALLO (salio con $RESGUI)"
    exit 1
fi
if [ $RES -eq 0 ]; then
    echo "interfaz en Linux: OK"
else
    echo "interfaz en Linux: FALLO (el WAV exportado no lleva el tono)"
fi
exit $RES
