#!/bin/sh
# El lienzo tiene que seguir al tema del escritorio.
#
#     sh probes/linux-tema.sh [build]
#
# Arranca la interfaz dos veces sobre un X virtual, una con el tema claro de GTK
# y otra con el oscuro, le hace una foto a cada una y mide el color del lienzo.
#
# Existe porque en Linux salia SIEMPRE claro: los controles nativos los pinta
# GTK y seguian al escritorio, pero el lienzo lo pinta SSB con `gui_alt_color`,
# y esa funcion resuelve el color EN EL MOMENTO DE REGISTRARLO. Si en ese
# momento el SDK todavia no sabe que el escritorio es oscuro, el color se queda
# claro para siempre. Ver docs/33.
BUILD=${1:-build-linux}
PANT=${PANT:-:97}
ANCHO=1000
ALTO=700

RAIZ=$(cd "$(dirname "$0")/.." && pwd) || exit 1
cd "$RAIZ" || exit 1

if [ ! -x "$BUILD/ssbgui" ]; then
    echo "no encuentro $BUILD/ssbgui"
    exit 1
fi
for t in Xvfb import convert python3; do
    command -v $t >/dev/null 2>&1 || { echo "falta $t (apt install xvfb imagemagick)"; exit 1; }
done

TRABAJO=$(mktemp -d /tmp/ssb-tema.XXXXXX)
XP=''
limpia() {
    [ -n "$XP" ] && kill $XP 2>/dev/null
    rm -rf "$TRABAJO"
}
trap limpia EXIT INT TERM

Xvfb "$PANT" -screen 0 "${ANCHO}x${ALTO}x24" >"$TRABAJO/xvfb.log" 2>&1 &
XP=$!
i=0
while [ $i -lt 40 ]; do
    DISPLAY="$PANT" xdpyinfo >/dev/null 2>&1 && break
    i=$((i + 1))
    sleep 0.25
done

printf 'wait 30\nquit\n' > "$TRABAJO/espera.ssb"

# La zona que se mide: bien dentro del lienzo, por debajo de las dos barras de
# herramientas y lejos del texto de ayuda, que es claro sobre oscuro y sesgaria
# la media.
ZX=380; ZY=300; ZW=240; ZH=160

mide() {
    _nombre=$1
    _tema=$2
    _espera=$3
    _dir="$TRABAJO/$_nombre"
    mkdir -p "$_dir"
    cd "$_dir" || return 1
    GTK_THEME="$_tema" DISPLAY="$PANT" \
        "$RAIZ/$BUILD/ssbgui" --lang en --script "$TRABAJO/espera.ssb" \
        >"$_dir/salida.txt" 2>&1 &
    _pid=$!

    # Esperar a que la ventana este de verdad en pantalla. Con un `sleep` fijo,
    # en una maquina lenta la foto sale del fondo negro del X virtual, y eso
    # ademas ENGANABA a la prueba: negro pasa por oscuro. Ver docs/33.
    _i=0
    while [ $_i -lt 60 ]; do
        if DISPLAY="$PANT" xwininfo -root -tree 2>/dev/null | grep -q 'SystemSoundBuffer'; then
            break
        fi
        _i=$((_i + 1))
        sleep 0.25
    done
    sleep 1

    DISPLAY="$PANT" import -window root "$_dir/shot.png" 2>/dev/null
    convert "$_dir/shot.png" -depth 8 "$_dir/shot.ppm" 2>/dev/null
    kill $_pid 2>/dev/null
    wait $_pid 2>/dev/null
    cd "$RAIZ" || return 1
    echo "GTK_THEME=$_tema  (se espera lienzo $_espera)"
    python3 probes/tema.py "$_dir/shot.ppm" $ZX $ZY $ZW $ZH "$_espera" | tee "$_dir/medida.txt"
    return ${PIPESTATUS:-$?}
}

mide claro  "Adwaita"      claro
R1=$?
echo
mide oscuro "Adwaita:dark" oscuro
R2=$?

# Los dos temas TIENEN que dar medidas distintas. Si salen iguales es que no se
# esta midiendo la ventana -por ejemplo el fondo del X virtual- y entonces que
# una de las dos cuadre no significa nada.
C1=$(sed -n 's/.*rgb(\([^)]*\)).*/\1/p' "$TRABAJO/claro/medida.txt" 2>/dev/null)
C2=$(sed -n 's/.*rgb(\([^)]*\)).*/\1/p' "$TRABAJO/oscuro/medida.txt" 2>/dev/null)
echo
if [ -z "$C1" ] || [ -z "$C2" ]; then
    echo "FALLO: no se pudo medir una de las dos capturas"
    exit 1
fi
if [ "$C1" = "$C2" ]; then
    echo "FALLO: los dos temas dan el mismo color, rgb($C1)."
    echo "       No se esta midiendo la ventana, asi que el resultado no vale."
    exit 1
fi

if [ $R1 -eq 0 ] && [ $R2 -eq 0 ]; then
    echo "tema en Linux: OK  (claro rgb($C1), oscuro rgb($C2))"
    exit 0
fi
echo "tema en Linux: FALLO"
exit 1
