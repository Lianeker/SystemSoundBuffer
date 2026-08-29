#!/bin/sh
# Comprueba que la captura en Linux graba DE VERDAD lo que suena.
#
#     sh probes/linux-tono.sh [segundos] [build]
#     SSB_SERVER=pipewire sh probes/linux-tono.sh
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
# de integracion continua.
SECS=${1:-10}
BUILD=${2:-build-linux}

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

TRABAJO=$(mktemp -d /tmp/ssb-tono.XXXXXX)
trap 'sonido_para; rm -rf "$TRABAJO"' EXIT INT TERM

sonido_arranca "$TRABAJO" || exit 1

echo "=== servidor ($SSB_SERVER) ==="
pactl info | grep -E "Server Name|Server Version|Default Sink"

echo
echo "=== fuentes que ve el motor ==="
"./$BUILD/ssb" list

echo
echo "=== tono de $SSB_FREQ Hz por $SSB_SINK, grabando $SECS s ==="
sonido_tono "$TRABAJO" $((SECS + 20)) || exit 1
"./$BUILD/ssb" rec --secs "$SECS" --buffer 60 --src output --out "$TRABAJO/buf"

WAV="$TRABAJO/buf/pista0.wav"
if [ ! -f "$WAV" ]; then
    echo "FALLO: no se genero $WAV"
    exit 1
fi

echo
echo "=== comprobacion de la senal ==="
python3 probes/tono.py "$WAV" "$SSB_FREQ"
RES=$?

echo
if [ $RES -eq 0 ]; then
    echo "captura en Linux ($SSB_SERVER): OK"
else
    echo "captura en Linux ($SSB_SERVER): FALLO"
fi
exit $RES
