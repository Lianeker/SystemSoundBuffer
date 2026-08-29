# 30 — PipeWire, y el sink que no existía

La mayoría de las distribuciones de hoy no corren PulseAudio puro sino
`pipewire-pulse`. Que el motor use `libpulse` no basta para decir que funciona
ahí: hasta que no se ejecuta, no se sabe. Esta ronda lo intenta, encuentra un
defecto real de camino, y se queda a medias por un tope del entorno de pruebas.

## Lo primero que se aprende: no pueden convivir

`pipewire-pulse` y `pulseaudio` son el mismo servicio y los paquetes se
excluyen — instalar el primero desinstala el segundo. Así que no pueden ser dos
pasos del mismo trabajo del CI: tienen que ser trabajos distintos, por fuerza.

## El arnés tuvo que cambiar

`module-sine` no existe en la capa de compatibilidad de PipeWire:

```
$ pactl load-module module-sine sink=ssbtono frequency=440
Failure: No such entity
```

`module-null-sink` sí. Así que el sink virtual sigue igual y el tono pasa a ser
un WAV que se reproduce con un solo `paplay`. Se genera a **480 Hz sobre 48000**,
que son exactamente 100 muestras por periodo: un segundo de tono se puede repetir
sin salto de fase en el empalme, y así un fichero de 90 s se escribe repitiendo
un segundo.

El montaje del servidor sale a `probes/linux-sonido.sh`, que se incluye desde las
dos pruebas y elige servidor con `SSB_SERVER=pulse|pipewire`. Antes estaba
copiado en las dos.

## El defecto que encontró

Con PipeWire, `ssb list` veía la fuente pero `ssb rec --src output` decía
`no encontrado`. La causa está en una línea del informe del servidor:

```
Default Sink: @DEFAULT_SINK@
```

PulseAudio devuelve el nombre real del dispositivo por omisión. PipeWire puede
devolver el marcador sin resolver —pasa cuando ninguna política de sesión ha
elegido uno— y encima **no lo resuelve si se le pregunta por él**:

```
$ pactl get-sink-volume @DEFAULT_SINK@
Failed to get sink information: No such entity
```

El backend construía `@DEFAULT_SINK@.monitor` y se rendía. Ahora cualquier nombre
que empiece por `@` se trata como marcador, no como dispositivo, y se cae al
primer sink que haya en vez de fallar. Lo mismo para la entrada, saltándose los
monitores, que no son entradas.

Esto no es solo cosa del banco de pruebas: un PipeWire recién instalado, sin
dispositivos configurados todavía, deja el mismo marcador. Antes de esto, SSB no
podía grabar ahí.

## Donde se paró

Con el arreglo, el nombre ya resuelve y el motor encuentra el sink. Pero el flujo
no arranca, y no por culpa del motor: **`parecord`, un cliente normal de
PulseAudio, tampoco captura nada** en ese servidor. La pista está en el
sink-input:

```
input  43  4294967295  42  PipeWire  s16le 2ch 48000Hz
         ^^^^^^^^^^ PA_INVALID_INDEX
```

El `paplay` no está enlazado a ningún sink. Y el motivo está en el registro:

```
failed to start systemd logind monitor: -2 (No such file or directory)
wireplumber ../src/main.c:364:on_disconnected: disconnected from pipewire
```

**wireplumber se muere.** Es el gestor de sesión: sin él, PipeWire acepta nodos
pero no encamina nada. Exige logind, y esta WSL no tiene systemd. Levantar un bus
de sistema de D-Bus a mano no basta; el que falta es logind.

O sea que el banco de pruebas no puede alojar PipeWire, no que PipeWire no
funcione. Los runners de GitHub sí llevan systemd.

## Lo que se deja montado

`.github/workflows/pipewire.yml`, en su propio fichero y **solo a mano**
(`workflow_dispatch`). No entra en la puerta de cada empujón hasta que se le haya
visto pasar: meter ahí algo sin verificar dejaría el CI en rojo por lo que no es
una regresión, y ponerle `continue-on-error` sería fabricar el verde vacuo contra
el que existe todo este trabajo. Cuando pase, se pliega dentro de `build.yml`
como un trabajo más.

## Estado

`ctest` verde en Windows y en Linux. Sobre PulseAudio, con el arnés nuevo y el
arreglo del marcador, las dos pruebas siguen pasando:

```
captura  480 Hz  dominio 2726968x   huecos 0
interfaz 480 Hz  dominio 1859837x   huecos 0
```

Sobre PipeWire: el arreglo del nombre está hecho y compilado, el flujo sigue sin
comprobarse. Es lo primero pendiente.
