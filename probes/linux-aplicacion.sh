#!/bin/sh
# Captura POR APLICACION en Linux: que grabe una y no la de al lado.
#
#     sh probes/linux-aplicacion.sh [segundos] [build]
#     SSB_SERVER=pipewire sh probes/linux-aplicacion.sh
#
# Suenan dos aplicaciones a la vez por el mismo sink, cada una con su tono:
#
#     ssb-objetivo   1320 Hz   <- la que se quiere grabar
#     ssb-otro        480 Hz   <- la que NO tiene que aparecer
#
# Se graba con `--src app:ssb-objetivo` y se exige que el WAV lleve 1320 y NO
# lleve 480. Esa segunda mitad es la que da valor a la prueba: grabar el monitor
# del sink entero tambien traeria el tono bueno, y pasaria una comprobacion que
# solo mirase si esta. Aqui, grabar de mas falla.
#
# Es la funcionalidad que distingue a SSB de una grabadora cualquiera, y en
# Linux va por `pa_stream_set_monitor_stream`, que acota el monitor del sink a
# un sink-input concreto.
SECS=${1:-10}
BUILD=${2:-build-linux}
FREQ_OBJ=1320
FREQ_OTRO=480

RAIZ=$(cd "$(dirname "$0")/.." && pwd) || exit 1
cd "$RAIZ" || exit 1
. "$RAIZ/probes/linux-sonido.sh"

if [ ! -x "$BUILD/ssb" ]; then
    echo "no encuentro $BUILD/ssb; compila primero o pasa el directorio como 2o argumento"
    exit 1
fi
for t in pactl paplay python3; do
    command -v $t >/dev/null 2>&1 || { echo "falta $t"; exit 1; }
done

TRABAJO=$(mktemp -d /tmp/ssb-app.XXXXXX)
trap 'sonido_para; rm -rf "$TRABAJO"' EXIT INT TERM

sonido_arranca "$TRABAJO" || exit 1

echo "=== dos aplicaciones sonando a la vez ==="
P1=$(sonido_tono_id "$TRABAJO" $((SECS + 25)) "$FREQ_OBJ"  ssb-objetivo)
P2=$(sonido_tono_id "$TRABAJO" $((SECS + 25)) "$FREQ_OTRO" ssb-otro)
SSB_TONO_PIDS="$P1 $P2"
sleep 2
pactl list short sink-inputs | sed 's/^/  /'

echo
echo "=== lo que ve el motor ==="
"./$BUILD/ssb" list

echo
echo "=== grabando solo ssb-objetivo ($FREQ_OBJ Hz), $SECS s ==="
"./$BUILD/ssb" rec --secs "$SECS" --buffer 60 --src app:ssb-objetivo --out "$TRABAJO/buf"

WAV="$TRABAJO/buf/pista0.wav"
if [ ! -f "$WAV" ]; then
    echo "FALLO: no se genero $WAV"
    echo
    sonido_diagnostico "$TRABAJO"
    exit 1
fi

echo
echo "=== tiene que llevar $FREQ_OBJ y NO llevar $FREQ_OTRO ==="
python3 probes/tono.py "$WAV" "$FREQ_OBJ" --sin "$FREQ_OTRO"
RES=$?

if [ $RES -ne 0 ]; then
    echo
    echo "captura por aplicacion en Linux ($SSB_SERVER): FALLO"
    sonido_diagnostico "$TRABAJO"
    exit 1
fi

# Control: grabar el sink ENTERO tiene que fallar la misma comprobacion. Si no
# fallara, la prueba de arriba estaria pasando por acompanamiento y no
# distinguiria "he grabado esta aplicacion" de "he grabado todo". Una puerta que
# no sabe ponerse roja no es una puerta.
echo
echo "=== control: el sink entero SI lleva los dos, asi que tiene que fallar ==="
"./$BUILD/ssb" rec --secs 6 --buffer 60 --src output --out "$TRABAJO/todo" >/dev/null 2>&1
if [ ! -f "$TRABAJO/todo/pista0.wav" ]; then
    echo "FALLO: el control no pudo grabar el sink entero"
    exit 1
fi
python3 probes/tono.py "$TRABAJO/todo/pista0.wav" "$FREQ_OBJ" --sin "$FREQ_OTRO"
CTRL=$?
if [ $CTRL -eq 0 ]; then
    echo
    echo "FALLO: el control paso. La comprobacion no distingue una aplicacion"
    echo "       del sink entero, asi que el OK de arriba no significa nada."
    exit 1
fi
echo "  el control falla, como tiene que ser"

echo
echo "captura por aplicacion en Linux ($SSB_SERVER): OK"
exit 0
