# Servidor de sonido virtual para las pruebas. No es ejecutable: se incluye.
#
#     . probes/linux-sonido.sh
#     sonido_arranca "$TRABAJO" || exit 1
#     sonido_tono "$TRABAJO" 90
#     ... la prueba ...
#     sonido_para
#
# SSB_SERVER=pulse (por omision) o pipewire. Existe porque la mayoria de las
# distribuciones de hoy no corren PulseAudio puro sino pipewire-pulse, y hasta
# que no se ejecuta ahi no se sabe si funciona.
#
# El servidor vive en su propio directorio de ejecucion: no toca la sesion de
# audio de quien lance la prueba en su portatil.
SSB_SERVER=${SSB_SERVER:-pulse}
SSB_SINK=${SSB_SINK:-ssbtono}
# 480 Hz a 48000 son 100 muestras por periodo exactas, asi que un segundo de
# tono se puede repetir sin salto de fase en el empalme.
SSB_FREQ=${SSB_FREQ:-480}
SSB_RATE=48000

sonido_arranca()
{
    _dir=$1
    SSB_SND_PIDS=''

    case "$SSB_SERVER" in
    pulse)
        command -v pulseaudio >/dev/null 2>&1 || { echo "falta pulseaudio"; return 1; }
        PULSE_RUNTIME_PATH="$_dir/pulse"
        export PULSE_RUNTIME_PATH
        unset PULSE_SERVER
        # -n: sin el guion de arranque por omision. Todo lo que carga esta
        # escrito aqui, asi que el servidor es el mismo en cualquier maquina.
        pulseaudio --daemonize=no -n --exit-idle-time=-1 \
            --load="module-native-protocol-unix" \
            --load="module-null-sink sink_name=$SSB_SINK sink_properties=device.description=SSB_tono" \
            --log-target=file:"$_dir/pulse.log" &
        SSB_SND_PIDS=$!
        ;;
    pipewire)
        for t in pipewire wireplumber pipewire-pulse; do
            command -v $t >/dev/null 2>&1 || { echo "falta $t"; return 1; }
        done
        XDG_RUNTIME_DIR="$_dir/run"
        mkdir -p "$XDG_RUNTIME_DIR"
        chmod 700 "$XDG_RUNTIME_DIR"
        export XDG_RUNTIME_DIR
        unset PULSE_RUNTIME_PATH PULSE_SERVER
        # Sin systemd hay que levantar los tres a mano, que es tambien la
        # situacion del runner. wireplumber es el gestor de sesion: sin el, el
        # sink se crea pero nadie lo encamina.
        pipewire       >"$_dir/pw.log"  2>&1 &  SSB_SND_PIDS="$!"
        wireplumber    >"$_dir/wp.log"  2>&1 &  SSB_SND_PIDS="$SSB_SND_PIDS $!"
        pipewire-pulse >"$_dir/pwp.log" 2>&1 &  SSB_SND_PIDS="$SSB_SND_PIDS $!"
        ;;
    *)
        echo "SSB_SERVER desconocido: $SSB_SERVER (vale pulse o pipewire)"
        return 1
        ;;
    esac

    _i=0
    while [ $_i -lt 80 ]; do
        pactl info >/dev/null 2>&1 && break
        _i=$((_i + 1))
        sleep 0.25
    done
    if ! pactl info >/dev/null 2>&1; then
        echo "el servidor de sonido no arranco ($SSB_SERVER)"
        for l in "$_dir"/pulse.log "$_dir"/pwp.log "$_dir"/pw.log; do
            [ -f "$l" ] && { echo "--- $l"; tail -15 "$l"; }
        done
        return 1
    fi

    # PipeWire no acepta modulos en la linea de ordenes del demonio, pero su
    # capa de compatibilidad si implementa module-null-sink.
    if [ "$SSB_SERVER" = pipewire ]; then
        pactl load-module module-null-sink sink_name="$SSB_SINK" \
              sink_properties=device.description=SSB_tono >/dev/null || return 1
    fi
    pactl set-default-sink "$SSB_SINK" >/dev/null 2>&1
    return 0
}

# El tono como fichero, no con module-sine: PipeWire no implementa ese modulo
# ("No such entity"), asi que la unica forma de sonar igual en los dos es
# reproducir un WAV. Un solo `paplay`, largo: varios seguidos serian varios
# sink-input distintos y eso estorba a la captura por aplicacion.
sonido_tono()
{
    _dir=$1
    _secs=${2:-90}
    _wav="$_dir/tono.wav"
    python3 - "$_wav" "$SSB_FREQ" "$_secs" "$SSB_RATE" <<'PY'
import math, struct, sys, wave
path, freq, secs, rate = sys.argv[1], float(sys.argv[2]), int(float(sys.argv[3])), int(sys.argv[4])
b = bytearray()
for i in range(rate):
    v = int(0.5 * 32767 * math.sin(2.0 * math.pi * freq * i / rate))
    b += struct.pack('<hh', v, v)
w = wave.open(path, 'wb')
w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
w.writeframes(bytes(b) * max(1, secs))
w.close()
PY
    [ -f "$_wav" ] || { echo "no se pudo generar el tono"; return 1; }
    paplay --device="$SSB_SINK" "$_wav" &
    SSB_TONO_PID=$!
    # Un momento para que suene antes de grabar: si se graba desde el primer
    # instante, el principio es silencio y el centro que analiza tono.py podria
    # caer ahi.
    sleep 1
    return 0
}

sonido_para()
{
    [ -n "$SSB_TONO_PID" ] && kill $SSB_TONO_PID 2>/dev/null
    [ -n "$SSB_SND_PIDS" ] && kill $SSB_SND_PIDS 2>/dev/null
    SSB_TONO_PID=''
    SSB_SND_PIDS=''
    # Un instante para que suelten los ficheros del directorio de trabajo: si
    # no, el `rm -rf` de quien llama se queja de directorio no vacio.
    sleep 1
}
