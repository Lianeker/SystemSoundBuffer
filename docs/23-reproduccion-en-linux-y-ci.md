# 23 — Reproducción en Linux, e integración continua

## Reproducción

`ssb_core/src/linux/ssb_play_linux.c`, con la API simple de libpulse. Los ocho
`ssb_play_*` que devolvían `ssb_err_platform` ya están.

Dos decisiones:

**API simple y no asíncrona.** La captura necesita un mainloop propio porque hay
que reaccionar a lo que manda el servidor. Reproducir es empujar bytes, y
`pa_simple_write` bloquea hasta que se aceptan. Con un hilo propio —el mismo
patrón que en Windows— eso basta y son 200 líneas menos.

**Se le pide al servidor el formato del fichero**, `PA_SAMPLE_S16LE` o
`PA_SAMPLE_S24LE` según lo que traiga el WAV. Reproducir es entonces copiar
bytes: no hay conversión que pueda estropear nada, y lo que se oye es
exactamente lo que se guardó.

El salto se aplica en el hilo de reproducción, no en el que lo pide, y va
seguido de `pa_simple_flush`: lo que ya estaba en el buffer del servidor
pertenece al tramo anterior y se oiría el sitio del que se venía.

El buffer se pide de 200 ms. La barra de reproducción se dibuja con lo
**entregado** al servidor, no con lo ya sonado, así que un buffer hondo la
adelanta respecto a lo que se oye.

### Cómo se comprobó

Que el programa escriba «reproduciendo» no demuestra nada. `probes/linux-reproduccion.sh`
graba 6 s, exporta el tramo, lo reproduce, y **mientras suena graba el monitor
del sink** con el propio motor:

```
=== el monitor del sink mientras la interfaz reproduce ===
  t=  3s | [0] 3.0s 190.7KB x2.7 pico 0.356
      2 ch a 44100 Hz, 5.04 s capturados
      discontinuidades 0, pico 0.3564
```

Pico 0.3564, el mismo que tenía la grabación original. Si la reproducción no
sonara, saldría cero.

La interfaz cierra con 1640 de 1640 asignaciones liberadas.

## Integración continua

`.github/workflows/build.yml`. Matriz de `windows-latest` y `ubuntu-latest`: en
cada una se clona la bifurcación del SDK, se compila e instala, se compila
SystemSoundBuffer, se pasa `ctest` y se enumeran las fuentes.

Dos cosas que no son obvias:

**`-DSSB_REQUIRE_GUI=ON`.** `find_package(nappgui QUIET ... NO_DEFAULT_PATH)` es
silencioso: si el SDK no apareciera, el CMake omitiría la interfaz con un mensaje
informativo y el job pasaría en verde **sin haber compilado la mitad del
proyecto**. Con la opción puesta, omitirla es un `FATAL_ERROR`. Es el mismo
agujero que en el proyecto de al lado dejó pasar dos avisos durante una tarea
entera (NAP-046 de `nappgui/backlog`): una comprobación que no puede fallar no
comprueba nada.

**Enumerar como último paso.** El runner no tiene servidor de sonido, así que no
se puede grabar. Lo que sí se puede es comprobar que `ssb list` no revienta ni se
cuelga sin servidor, que es el primer camino que toca el sistema de audio al
arrancar.

Lo que el CI **no** cubre: capturar y reproducir de verdad. Eso necesita un
servidor de sonido y sigue siendo manual, con las sondas.

## Estado

- Linux: motor, CLI e interfaz compilan sin un aviso con `-Wall -Wextra -Werror`.
  `ctest` verde. Captura y reproducción comprobadas escuchando el resultado.
- Windows: sin cambios. `ctest` verde en Debug y Release, `ssb list` enumera 8
  fuentes.
- Sin comprobar: PipeWire, y cómo se ve la interfaz en GTK.
