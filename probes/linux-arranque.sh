#!/bin/sh
# Arranca y cierra la interfaz N veces y cuenta los fallos.
#
#     sh probes/linux-arranque.sh [veces] [build]
#
# Existe porque la primera vez que se ejecuto con un display de verdad, la
# interfaz murio con SIGSEGV. Bajo gdb no volvio a pasar, asi que hace falta
# repetir: un fallo que aparece una de cada tantas no se ve ejecutando una vez.
#
# Necesita un servidor grafico. En WSL:
#     export XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir
#     export WAYLAND_DISPLAY=wayland-0 DISPLAY=:0
VECES=${1:-8}
BUILD=${2:-build-linux}

cd "$(dirname "$0")/.." || exit 1

if [ -z "$XDG_RUNTIME_DIR" ] && [ -d /mnt/wslg/runtime-dir ]; then
    XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir
    WAYLAND_DISPLAY=wayland-0
    DISPLAY=:0
    export XDG_RUNTIME_DIR WAYLAND_DISPLAY DISPLAY
fi

printf 'add output\nwait 2\nquit\n' > /tmp/arranque.ssb

ok=0
bad=0
i=1
while [ "$i" -le "$VECES" ]; do
    "./$BUILD/ssbgui" --lang en --script /tmp/arranque.ssb > /dev/null 2>&1
    rc=$?
    if [ "$rc" -eq 0 ]; then
        ok=$((ok + 1))
    else
        bad=$((bad + 1))
        echo "  intento $i: codigo $rc"
        if [ "$rc" -gt 128 ]; then
            echo "    (senal $((rc - 128)))"
        fi
    fi
    i=$((i + 1))
done

echo "arranques limpios: $ok de $VECES, con fallo: $bad"
[ "$bad" -eq 0 ]
