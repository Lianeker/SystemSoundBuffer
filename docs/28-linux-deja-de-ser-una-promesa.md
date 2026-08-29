# 28 — Linux deja de ser una promesa

La tabla del README decía que en Linux se captura con PulseAudio, se reproduce
la selección y hay interfaz GTK3. Ninguna de esas tres celdas la respaldaba una
ejecución: el CI compilaba, pasaba el selftest —que es códec, anillo y mezcla,
sin audio— y enumeraba fuentes sin servidor de sonido. Todo lo demás era
confianza.

Esta ronda no añade funcionalidad a Linux. Construye el instrumento que dice si
la que hay funciona.

## El problema del runner sin tarjeta

Un runner de integración continua no tiene tarjeta de sonido ni pantalla, que es
justo por lo que nunca se había comprobado nada. La salida es traerse las dos:

```sh
pulseaudio --daemonize=no -n --exit-idle-time=-1 \
    --load="module-native-protocol-unix" \
    --load="module-null-sink sink_name=ssbtono ..."
pactl load-module module-sine sink=ssbtono frequency=440
```

El `-n` importa: sin el guion de arranque por omisión, todo lo que carga el
servidor está escrito ahí y es el mismo en cualquier máquina. Y el servidor vive
en su propio `PULSE_RUNTIME_PATH`, así que no toca la sesión de audio de quien
ejecute la prueba en su portátil.

## Grabar no es comprobar

Que salga un WAV no dice nada: un WAV de silencio también sale. Lo que separa
«grabó el tono» de «grabó algo» es comparar contra frecuencias señuelo.

`probes/tono.py` hace un Goertzel a mano sobre el módulo `wave` de la biblioteca
estándar —añadir numpy solo para esto sería una dependencia nueva por la puerta
de atrás— y exige tres cosas: que haya señal, que la frecuencia buscada domine
por 20× al mejor señuelo, y que la duración cuadre. Los señuelos son múltiplos
**no enteros** de la frecuencia: un armónico de la señal real puntuaría alto y
ensuciaría la comparación.

Primera ejecución, `probes/linux-tono.sh`:

```
  44100 Hz, 2 canal(es), 16 bits, 9.098 s
  RMS 0.35353
     440.0 Hz  6.249e-02   <- buscado
     180.4 Hz  1.530e-08
     277.2 Hz  7.757e-08
     602.8 Hz  4.105e-08
    1007.6 Hz  8.311e-10
    1588.4 Hz  1.501e-10
  dominio 805619x
```

Con 0 discontinuidades y 0 bloques descartados. Es la primera vez que la captura
en Linux queda demostrada de punta a punta.

## La puerta tiene que saber ponerse roja

Un verde que no puede fallar no es una puerta. Ya nos pasó con la de avisos del
SDK (`NAP-046`), que miraba un build incremental y por eso siempre daba cero.

Así que el verificador se validó contra tres grabaciones sintéticas que **tienen**
que fallar, y una que tiene que pasar:

| entrada | buscando | salida |
|---|---|---|
| silencio | 440 Hz | 1 |
| tono de 1000 Hz | 440 Hz | 1 |
| 440 Hz a amplitud 0.002 | 440 Hz | 1 |
| tono de 1000 Hz | 1000 Hz | 0 |

## La interfaz: el ciclo entero, no abrir y cerrar

`probes/linux-arranque.sh` ya abría y cerraba la ventana contando caídas. Eso no
prueba que grabe. `probes/linux-interfaz.sh` arranca la interfaz bajo `Xvfb` y le
da por guion el ciclo completo —añadir la fuente, grabar, seleccionar cinco
segundos, exportar— y después comprueba que **el WAV que escribió la interfaz**
lleva el tono.

```
> add output
pista 1: SSB_tono
> rec
> save 5
[msg] Saved 1 of 1 tracks, 5.00 s each: .../2026-08-29_12-57-16-N.wav
...
  dominio 861922x
  OK: el tono de 440 Hz esta en la grabacion
interfaz en Linux: OK
```

Ahora un fallo en cualquier eslabón —la ventana, la captura, la selección, la
exportación en su hilo propio— sale como fallo.

## Lo que encontró nada más encenderse

El SDK imprime al salir un informe de recursos, y en Linux se ve:

```
Non-dealloc Mutex: 3/2
Non-dealloc Threads: 1/0
```

El hilo es de esta ronda anterior: la exportación se mudó a un hilo propio en
`docs/26`, y `i_destroy` nunca lo cerraba. Mirando por qué, resulta que no era
una fuga sino algo peor. `bthread_close` **no espera**, solo suelta el descriptor
(`osbs/win/bthread.c:44-53`), y `i_destroy` destruye las pistas y la `App` que el
hilo está leyendo. Cerrar la ventana mientras se exporta un tramo largo —que es
exactamente la operación que tarda segundos— leía memoria ya liberada.

```c
if ((*app)->job.th != NULL)
{
    bthread_wait((*app)->job.th);
    bthread_close(&(*app)->job.th);
}
```

Es la misma forma que `docs/24`: un backend nuevo enseña un defecto que estaba en
el código portable y que en Windows no se veía. Aquí ni siquiera hizo falta que
fallara — bastó con que alguien contara los descriptores al salir.

## Lo que queda abierto

- **`huecos 1`** en la ejecución de la interfaz: una re-anclada de la línea de
  tiempo en unos 12 s, o sea que la fuente perdió audio una vez. En la captura por
  línea de comandos salen 0. **Se reproduce igual en el runner**, que es una
  máquina limpia sin WSL de por medio, así que no es el planificador.
  **Resuelto en [`docs/29`](29-el-paquete-corto-no-es-fresco.md)**: el backend
  sellaba tarde los paquetes cortos de PulseAudio, y el hueco lo fabricaba el
  sello. De paso, «en la línea de comandos salen 0» era falso — el CLI imprimía
  discontinuidades, no re-anclas, así que la comparación no estaba hecha.
- **`Non-dealloc Mutex: 3/2`**: queda un mutex sin destruir. No es el de la pista
  —esos se destruyen— así que está en otro sitio. También se reproduce en el
  runner.
- **PipeWire**: la mayoría de distribuciones de hoy no corren PulseAudio puro sino
  `pipewire-pulse`. Con este arnés, comprobarlo cuesta una fila más de la matriz.
  Sigue sin comprobarse.
- **Captura por aplicación en Linux**: el arnés puede levantar un segundo
  reproductor haciendo de «aplicación» y comprobar que se captura solo ese. Es el
  siguiente paso de paridad con Windows.
- **MP3 y AAC**: `ssb_encode` devuelve `ssb_err_platform` en Linux
  (`ssb_util.c:369`). Falta comprobar que la interfaz cae a WAV limpiamente en vez
  de dar error.

## Estado

`ctest` verde en Windows y en Linux. `probes/linux-tono.sh` y
`probes/linux-interfaz.sh` en verde, y validados contra entradas que tienen que
fallar. Los dos entran en el CI: el primero en el trabajo del motor, el segundo en
el de la interfaz.

Primera ejecución en el runner, con los cuatro trabajos en verde:

```
motor    linux-x64-gcc   dominio  856899x   discontinuidades 0
interfaz linux-x64-gcc   dominio 1067610x   huecos 1
```
