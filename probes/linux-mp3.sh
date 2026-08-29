#!/bin/sh
# El MP3 de Linux: que codifique, y que lo codificado sea el audio de verdad.
#
#     sh probes/linux-mp3.sh [build]
#
# Genera un WAV con un tono conocido, lo pasa a MP3 con `ssb encode`, lo vuelve
# a decodificar con mpg123 y comprueba que el tono sigue ahi. Sin el viaje de
# vuelta esto solo comprobaria que sale un fichero, y un fichero de silencio
# tambien sale.
#
# Se prueban las tres formas que puede tener lo que exporta el programa: 16 y
# 24 bits, estereo y mono.
#
# Necesita mpg123 SOLO para la prueba. El programa no lo usa: carga libmp3lame
# en tiempo de ejecucion con dlopen.
BUILD=${1:-build-linux}
FREQ=480

RAIZ=$(cd "$(dirname "$0")/.." && pwd) || exit 1
cd "$RAIZ" || exit 1

if [ ! -x "$BUILD/ssb" ]; then
    echo "no encuentro $BUILD/ssb"
    exit 1
fi
command -v mpg123 >/dev/null 2>&1 || { echo "falta mpg123 (apt install mpg123)"; exit 1; }

T=$(mktemp -d /tmp/ssb-mp3.XXXXXX)
trap 'rm -rf "$T"' EXIT

if [ ! -e /usr/lib/x86_64-linux-gnu/libmp3lame.so.0 ] && ! ldconfig -p 2>/dev/null | grep -q libmp3lame; then
    echo "libmp3lame no esta instalada: el motor caera a WAV, que es lo previsto."
    echo "Para probar el camino de MP3:  sudo apt install libmp3lame0"
    exit 1
fi

caso() {
    _nombre=$1
    _bits=$2
    _canales=$3
    _w="$T/$_nombre.wav"
    _m="$T/$_nombre.mp3"
    _d="$T/$_nombre-vuelta.wav"

    python3 - "$_w" "$FREQ" "$_bits" "$_canales" <<'PY'
import math, struct, sys, wave
path, freq, bits, ch = sys.argv[1], float(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
rate, secs = 48000, 4
w = wave.open(path, 'wb')
w.setnchannels(ch); w.setsampwidth(bits // 8); w.setframerate(rate)
b = bytearray()
tope = (1 << (bits - 1)) - 1
for i in range(rate * secs):
    v = int(0.5 * tope * math.sin(2.0 * math.pi * freq * i / rate))
    if bits == 16:
        m = struct.pack('<h', v)
    else:
        m = struct.pack('<i', v)[:3]
    b += m * ch
w.writeframes(bytes(b))
w.close()
PY

    "./$BUILD/ssb" encode "$_w" "$_m" 192 >"$T/$_nombre.log" 2>&1
    _r=$?
    if [ $_r -ne 0 ] || [ ! -s "$_m" ]; then
        echo "  $_nombre: FALLO al codificar"
        sed 's/^/    /' "$T/$_nombre.log"
        return 1
    fi

    mpg123 -q -w "$_d" "$_m" >/dev/null 2>&1
    if [ ! -s "$_d" ]; then
        echo "  $_nombre: FALLO al decodificar el MP3"
        return 1
    fi

    python3 probes/tono.py "$_d" "$FREQ" >"$T/$_nombre.chk" 2>&1
    _c=$?
    printf '  %-12s  wav %8s B  ->  mp3 %7s B   %s\n' "$_nombre" \
           "$(stat -c%s "$_w")" "$(stat -c%s "$_m")" \
           "$(grep -o 'dominio [0-9]*x' "$T/$_nombre.chk")"
    if [ $_c -ne 0 ]; then
        sed 's/^/    /' "$T/$_nombre.chk"
        return 1
    fi
    return 0
}

echo "=== tono de $FREQ Hz por el codificador ==="
R=0
caso estereo-16 16 2 || R=1
caso estereo-24 24 2 || R=1
caso mono-16    16 1 || R=1

echo
echo "=== y el formato que no esta ==="
"./$BUILD/ssb" encode "$T/estereo-16.wav" "$T/x.m4a" 192 >"$T/aac.log" 2>&1
if [ -s "$T/x.m4a" ]; then
    echo "  FALLO: se genero un m4a y AAC no esta implementado"
    R=1
else
    echo "  m4a: no se genera, como debe ser"
fi

echo
if [ $R -eq 0 ]; then
    echo "mp3 en Linux: OK"
else
    echo "mp3 en Linux: FALLO"
fi
exit $R
