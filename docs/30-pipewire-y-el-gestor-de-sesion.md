# 30 — PipeWire, y el gestor de sesión que se moría

La mayoría de las distribuciones de hoy no corren PulseAudio puro sino
`pipewire-pulse`. Que el motor use `libpulse` no basta para decir que funciona
ahí. Ahora se ejecuta, y pasa.

```
Server Name: PulseAudio (on PipeWire 1.0.5)
captura   480 Hz  dominio 2099365x  reanclas 0  discontinuidades 0
interfaz  480 Hz  dominio 3162951x  huecos 0
```

## Lo primero: no pueden convivir

`pipewire-pulse` y `pulseaudio` son el mismo servicio y los paquetes se
excluyen — instalar el primero desinstala el segundo. Así que no pueden ser dos
pasos del mismo trabajo del CI: tienen que ser trabajos distintos, por fuerza.
Eso decidió la forma de todo lo demás.

## El arnés tuvo que cambiar

`module-sine` no existe en la capa de compatibilidad de PipeWire:

```
$ pactl load-module module-sine sink=ssbtono frequency=440
Failure: No such entity
```

`module-null-sink` sí. Así que el sink virtual sigue igual y el tono pasa a ser
un WAV que se reproduce con un solo `paplay`. Se genera a **480 Hz sobre 48000**:
exactamente 100 muestras por periodo, así que un segundo de tono se repite sin
salto de fase en el empalme y un fichero de 90 s se escribe repitiendo un
segundo.

El montaje del servidor sale a `probes/linux-sonido.sh`, común a las dos pruebas,
que elige con `SSB_SERVER=pulse|pipewire`. Antes estaba copiado en las dos.

## El síntoma, y las dos veces que lo leí mal

Con PipeWire, `ssb list` veía la fuente pero grabar fallaba con
`Stream error: Timeout`. Y el informe del servidor decía:

```
Default Sink: @DEFAULT_SINK@
```

Un marcador sin resolver, que además PipeWire **no resuelve** si se le pregunta
(`pactl get-sink-volume @DEFAULT_SINK@` → `No such entity`). Parecía la causa.

No lo era. Lo que lo demostró: **`parecord`, un cliente normal de PulseAudio,
tampoco capturaba nada**. Si un cliente ajeno falla igual, el problema no está en
nuestro código.

La pista real estaba en el sink-input:

```
input  43  4294967295  42  PipeWire  s16le 2ch 48000Hz
           ^^^^^^^^^^ PA_INVALID_INDEX: sin enlazar a ningun sink
```

El segundo error de lectura fue dar por hecho que era cosa de WSL. En WSL el
registro decía `failed to start systemd logind monitor`, no hay systemd allí, y
la conclusión fácil era «en un runner de verdad irá». **Falló igual en el
runner.**

## La causa

Lo dijo el diagnóstico en cuanto se añadió, y era distinta de la de WSL:

```
wireplumber MUERTO
  Error acquiring bus address: Cannot autolaunch D-Bus without X11 $DISPLAY
```

**wireplumber necesita un bus de sesión de D-Bus.** Sin él se muere al arrancar, y
sin gestor de sesión PipeWire acepta nodos pero no encamina nada: el sink se
queda `SUSPENDED`, los flujos no se enlazan y el monitor no entrega nada. De ahí
el `@DEFAULT_SINK@` —nadie había elegido un dispositivo por omisión— y de ahí el
tiempo de espera agotado. Un solo fallo con tres síntomas.

El arnés levanta ahora su propio bus y lo recoge al terminar:

```sh
dbus-daemon --session --fork --print-address=3 --print-pid=4 \
    3>"$_dir/dbus.addr" 4>"$_dir/dbus.pid"
```

Con eso, `Default Sink: ssbtono` y todo enlaza.

## El arreglo del motor sigue estando bien, pero no era esto

`i_name_usable`: cualquier nombre que empiece por `@` es un marcador, no un
dispositivo, y ahora se cae al primer sink en vez de rendirse. Lo mismo para la
entrada, saltándose los monitores, que no son entradas.

Con el gestor de sesión vivo ese marcador ya no aparece, así que **este arreglo no
era lo que bloqueaba PipeWire**. Se queda porque es correcto: antes el motor se
rendía ante un nombre que no podía resolver, y ahora usa lo que hay. Cubre el caso
de un PipeWire sin política de sesión, que es exactamente el que se acaba de ver.
No se vende como más de lo que es.

## Lo que hacía falta y no había

Nada de esto se habría encontrado sin `sonido_diagnostico`, que se escribió a
mitad justamente porque estaba adivinando. Vuelca el estado del servidor —sinks,
sources y sink-inputs con su enlace—, si los tres procesos de PipeWire siguen
vivos, `wpctl status` y la cola de los registros. Dio la causa a la primera
ejecución.

## Estado

Tres trabajos en la puerta: motor e interfaz sobre PulseAudio, y motor e interfaz
sobre PipeWire. El fichero `pipewire.yml` que se dejó suelto y a mano mientras no
se le había visto pasar ya no existe: se plegó en `build.yml`, que era la promesa.

Sigue abierto de `docs/28`: el mutex sin destruir (`3/2`) y la captura por
aplicación en Linux sin prueba automática. Esta última ya es barata — el arnés
puede levantar un segundo reproductor que haga de aplicación.
