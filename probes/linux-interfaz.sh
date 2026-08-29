#!/bin/sh
# El ciclo completo de la interfaz en Linux, sin pantalla y sin tarjeta.
#
#     sh probes/linux-interfaz.sh [build]
#     SSB_SERVER=pipewire sh probes/linux-interfaz.sh
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

RAIZ=$(cd "$(dirname "$0")/.." && pwd) || exit 1
cd "$RAIZ" || exit 1
. "$RAIZ/probes/linux-sonido.sh"

if [ ! -x "$BUILD/ssbgui" ]; then
    echo "no encuentro $BUILD/ssbgui; compila con el SDK o pasa el directorio como argumento"
    exit 1
fi
for t in pactl paplay xvfb-run python3; do
    command -v $t >/dev/null 2>&1 || { echo "falta $t"; exit 1; }
done

TRABAJO=$(mktemp -d /tmp/ssb-interfaz.XXXXXX)
trap 'sonido_para; rm -rf "$TRABAJO"' EXIT INT TERM

sonido_arranca "$TRABAJO" || exit 1
sonido_tono "$TRABAJO" 60 || exit 1

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

echo "=== la interfaz, bajo Xvfb, con $SSB_SERVER ==="
cd "$TRABAJO" || exit 1
xvfb-run -a "$RAIZ/$BUILD/ssbgui" --lang en --script "$TRABAJO/ciclo.ssb" \
    >"$TRABAJO/gui.out" 2>&1
RESGUI=$?
cd "$RAIZ" || exit 1
cat "$TRABAJO/gui.out"

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
    echo
    sonido_diagnostico "$TRABAJO"
    exit 1
fi

echo "=== comprobacion de lo que exporto la interfaz ==="
python3 "$RAIZ/probes/tono.py" "$WAV" "$SSB_FREQ"
RESTONO=$?

# El SDK cuenta al salir los recursos que se quedaron sin soltar. Ese informe
# encontro dos defectos de verdad —un hilo que seguia leyendo memoria liberada
# (docs/28) y el cerrojo de ese hilo (docs/31)— y no lo miraba nadie: se leia de
# casualidad al mirar el registro. Ahora es una puerta.
RESREC=0
if grep -q "Non-dealloc" "$TRABAJO/gui.out"; then
    RESREC=1
fi

echo
if [ $RESGUI -ne 0 ]; then
    echo "interfaz en Linux ($SSB_SERVER): FALLO (salio con $RESGUI)"
    exit 1
fi
if [ $RESTONO -ne 0 ]; then
    echo "interfaz en Linux ($SSB_SERVER): FALLO (el WAV exportado no lleva el tono)"
fi
if [ $RESREC -ne 0 ]; then
    echo "interfaz en Linux ($SSB_SERVER): FALLO (recursos sin soltar al salir)"
    grep "Non-dealloc" "$TRABAJO/gui.out" | sed 's/^/  /'
fi
if [ $RESTONO -eq 0 ] && [ $RESREC -eq 0 ]; then
    echo "interfaz en Linux ($SSB_SERVER): OK"
    exit 0
fi
exit 1
