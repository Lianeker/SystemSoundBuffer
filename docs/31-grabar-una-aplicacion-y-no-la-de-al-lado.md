# 31 — Grabar una aplicación, y no la de al lado

Capturar el audio de **una** aplicación es lo que distingue a SSB de una
grabadora cualquiera. En Windows va por loopback por proceso; en Linux, por
`pa_stream_set_monitor_stream`, que acota el monitor de un sink a un sink-input
concreto. Estaba escrito desde `docs/22` y nunca se había ejecutado en una
prueba automática.

Ahora sí, y en los dos servidores de sonido.

## La prueba tenía que poder equivocarse

Comprobar «el WAV lleva el tono de la aplicación» no vale para nada: grabar el
sink entero también lo llevaría. Una comprobación que solo mira si el tono está
la pasa cualquier grabación de más.

Así que suenan **dos** aplicaciones a la vez por el mismo sink:

| | tono | qué es |
|---|---|---|
| `ssb-objetivo` | 1320 Hz | la que se quiere grabar |
| `ssb-otro` | 480 Hz | la que no puede aparecer |

Se graba con `--src app:ssb-objetivo` y se exige que el WAV lleve 1320 **y no
lleve 480**. `tono.py` gana una opción `--sin` que mete esas frecuencias entre
los señuelos, así que el tono buscado tiene que dominarlas igual que a los demás.

```
    1320.0 Hz  6.248e-02   <- buscado
     480.0 Hz  2.223e-33   <- no puede estar
  dominio 25043725x
```

Treinta órdenes de magnitud. No es que el otro tono quede por debajo: es que no
está.

## Y el control, dentro de la propia prueba

Queda la duda de si esa comprobación distingue de verdad o pasa por
acompañamiento. La prueba se la responde sola: después del caso bueno graba el
sink **entero** con los dos tonos sonando y exige que la misma comprobación
**falle**.

```
=== control: el sink entero SI lleva los dos, asi que tiene que fallar ===
    1320.0 Hz  6.248e-02   <- buscado
     480.0 Hz  6.248e-02   <- no puede estar
  dominio 1x
  FALLO: el tono no domina: 1.0x sobre el mejor senuelo, hace falta 20x
  el control falla, como tiene que ser
```

Las dos amplitudes idénticas. Si la captura por aplicación degenerara alguna vez
en capturar todo el sink, esto se pondría rojo.

## Cómo se simulan dos aplicaciones

El motor identifica una aplicación por `application.process.binary`, y si no,
por `application.name` (`ssb_capture_linux.c`, `i_cb_sinkinput`). Dos `paplay`
serían la misma cosa, así que cada uno se lanza con

```sh
paplay --property=application.process.binary=ssb-objetivo ...
```

que es exactamente la propiedad por la que se distinguirían dos programas de
verdad.

## Una hora perdida por una tubería

El lanzador de tonos devuelve el pid, así que se llama dentro de una sustitución
de órdenes. Y ahí `paplay` heredaba como salida la tubería de esa sustitución:
al cerrarse, moría de SIGPIPE en cuanto escribía algo. El síntoma era
`app:ssb-objetivo: no encontrado`, que parecía del motor y no lo era — no había
ninguna aplicación sonando.

Se arregla mandando su salida a `/dev/null`, y el mismo fallo habría alcanzado a
las otras dos pruebas, que pasaron a usar el mismo lanzador en esta ronda.

## Estado

Tres pruebas de Linux en la puerta, cada una en los dos servidores donde aplica:

```
                        PulseAudio      PipeWire
captura del sink        dominio 2.9M    dominio 2.1M
captura por aplicacion  dominio 25.0M   (en el CI)
ciclo de la interfaz    dominio 3.1M    dominio 3.2M
```

Con `huecos 0`, `reanclas 0` y `discontinuidades 0` en todas.

De la lista de `docs/28` ya solo queda el mutex sin destruir (`3/2`) que el SDK
reporta al salir.
