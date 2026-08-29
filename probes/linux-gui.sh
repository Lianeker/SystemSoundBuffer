#!/bin/sh
# Abre la interfaz en Linux para mirarla con los ojos.
#
#     sh probes/linux-gui.sh              # la abre y ya
#     sh probes/linux-gui.sh --tono       # ademas pone un tono a sonar
#     sh probes/linux-gui.sh --tono 300   # el tono dura 300 s
#
# Las otras pruebas de Linux corren bajo Xvfb, que no tiene pantalla: sirven
# para comprobar que funciona, no para ver como queda. Esto usa el compositor
# de verdad.
#
# En WSL eso es WSLg, que ya exporta DISPLAY, WAYLAND_DISPLAY, XDG_RUNTIME_DIR y
# PULSE_SERVER. Su servidor de sonido ofrece:
#
#     salida  -> RDPSink.monitor, o sea lo que suene DENTRO de WSL.
#                El audio de Windows NO pasa por aqui.
#     entrada -> RDPSource, que si es el microfono de Windows.
#
# Por eso esta la opcion --tono: sin nada sonando dentro de WSL, la pista de
# salida sale plana y no se ve nada en la onda.
TONO=0
SECS=180
BUILD=${BUILD:-build-linux}

for a in "$@"; do
    case "$a" in
    --tono) TONO=1 ;;
    [0-9]*) SECS=$a ;;
    *) BUILD=$a ;;
    esac
done

RAIZ=$(cd "$(dirname "$0")/.." && pwd) || exit 1
cd "$RAIZ" || exit 1

if [ ! -x "$BUILD/ssbgui" ]; then
    echo "no encuentro $BUILD/ssbgui"
    echo "compila con:  cmake -S . -B $BUILD -G Ninja -DCMAKE_BUILD_TYPE=Release \\"
    echo "                    -DSSB_NAPPGUI_DIR=../nappgui/install-linux/cmake"
    exit 1
fi

# WSLg lo deja puesto solo, pero si se entra por otra via (ssh, sudo a otro
# usuario) hay que rehacerlo a mano.
if [ -z "$WAYLAND_DISPLAY" ] && [ -d /mnt/wslg/runtime-dir ]; then
    XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir
    WAYLAND_DISPLAY=wayland-0
    DISPLAY=:0
    export XDG_RUNTIME_DIR WAYLAND_DISPLAY DISPLAY
fi
if [ -z "$PULSE_SERVER" ] && [ -S /mnt/wslg/PulseServer ]; then
    PULSE_SERVER=unix:/mnt/wslg/PulseServer
    export PULSE_SERVER
fi

if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
    echo "no hay pantalla: ni DISPLAY ni WAYLAND_DISPLAY."
    echo "Para una prueba sin pantalla usa probes/linux-interfaz.sh, que va con Xvfb."
    exit 1
fi

echo "pantalla:  DISPLAY=$DISPLAY  WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
echo "sonido:    $(pactl info 2>/dev/null | grep 'Server Name' || echo 'sin servidor')"
echo
echo "fuentes que vas a encontrar en el desplegable:"
"./$BUILD/ssb" list 2>&1 | sed 's/^/  /'

TRABAJO=$(mktemp -d /tmp/ssb-gui.XXXXXX)
TP=''
limpia() {
    [ -n "$TP" ] && kill $TP 2>/dev/null
    rm -rf "$TRABAJO"
}
trap limpia EXIT INT TERM

if [ $TONO -eq 1 ]; then
    echo
    echo "generando $SECS s de tono de 480 Hz..."
    python3 - "$TRABAJO/tono.wav" "$SECS" <<'PY'
import math, struct, sys, wave
path, secs, rate = sys.argv[1], int(sys.argv[2]), 48000
b = bytearray()
for i in range(rate):
    v = int(0.4 * 32767 * math.sin(2.0 * math.pi * 480.0 * i / rate))
    b += struct.pack('<hh', v, v)
w = wave.open(path, 'wb')
w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
w.writeframes(bytes(b) * max(1, secs))
w.close()
PY
    paplay --property=application.process.binary=ssb-tono "$TRABAJO/tono.wav" \
        >/dev/null 2>&1 &
    TP=$!
    echo "sonando. En la interfaz: anade 'output' para verlo, o 'ssb-tono'"
    echo "para probar la captura por aplicacion."
fi

echo
echo "abriendo la interfaz. El buffer se crea en $TRABAJO."
cd "$TRABAJO" || exit 1
"$RAIZ/$BUILD/ssbgui" --lang "${SSB_LANG:-es}"
echo
echo "cerrada."
