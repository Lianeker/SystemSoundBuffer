# 29 — El paquete corto no es fresco

`docs/28` dejó abierto un `huecos 1`: la interfaz en Linux re-anclaba la línea de
tiempo una vez por ejecución, o sea que decía haber perdido audio. Se reproducía
en el runner, así que no era WSL. Esta es la causa.

## Dos hipótesis descartadas antes de la buena

**La rendija de la pausa.** `ssbapp.c:225` pausa la pista justo después de
crearla, y `ssb_track_create` ya ha arrancado el hilo de captura; en esa rendija
puede colarse audio, y al reanudar con `rec` eso cuenta como re-ancla
(`ssb_track.c:420`). Si fuera eso, grabar **antes** de añadir la fuente daría 0,
porque en ese orden no hay pausa ninguna.

```
  add-luego-rec-1        huecos 1      rec-luego-add-1        huecos 1
  add-luego-rec-2        huecos 1      rec-luego-add-2        huecos 1
  add-luego-rec-3        huecos 1      rec-luego-add-3        huecos 1
```

No era.

**Que fuera cosa de la interfaz.** El CLI imprimía `discontinuidades` pero no las
re-anclas, así que la comparación que se creía hecha no lo estaba. Añadido el
dato, el CLI sobre el mismo tono daba `reanclas 1` en dos de tres ejecuciones.
Tampoco era la interfaz: era el backend.

## La causa

`ssb drift` existe justo para separar un escalón de arranque de una pendiente de
reloj (`docs/03`). Sobre 12 s de tono:

```
0,13,0.2596,11466,882,0,-0.3757
0,14,0.2970,12348,118,0,16.9876      <- paquete de 118 frames, +17 ms
0,15,0.2845,12466,668,0, 1.8569      <- y su marca va HACIA ATRAS
```

46 saltos de más de 5 ms en 12 s, y **todos** coincidiendo con un paquete corto.

`ssb_capture_linux.c` sellaba cada paquete con `ahora - su duración`. Eso solo
vale si el paquete trae datos recién producidos. PulseAudio entrega a menudo un
trozo largo y un resto corto, y el corto **no es fresco**: es la cola de lo que ya
estaba disponible, que ha esperado. Restarle solo sus 2,7 ms lo coloca hasta 17 ms
tarde — y por detrás del paquete que lo precede.

El listón del motor son 8 ms sostenidos durante tres paquetes
(`ssb_track.c:26-27`). Las desviaciones llegaban a 20 ms y venían en rachas, así
que tarde o temprano una racha lo cruzaba y la línea de tiempo se re-anclaba. El
hueco no existía: lo fabricaba el sello.

## El arreglo, y por qué solo corrige en un sentido

Un flujo continuo avanza exactamente `frames`. Si el sello cae **después** de
donde acabó el paquete anterior y la diferencia es pequeña, manda la continuidad:

```c
if (c->t_next != 0 && t > c->t_next && (t - c->t_next) < slack)
    t = c->t_next;
c->t_next = t + dur;
```

Solo en ese sentido. Si el sello cae **antes**, es que la fuente va rápida —deriva,
no hueco— y ahí manda el reloj. Corregir también hacia adelante convertiría esto
en una cuenta de frames a secas, que es exactamente el error contra el que avisa
`docs/03`: sin el reloj, la línea de tiempo se separa sola y no vuelve.

El margen son 40 ms: cubre las desviaciones medidas sin tragarse un corte real,
que por encima de eso sigue llegando al motor como lo que es. Y un hueco que
anuncia el servidor (`pa_stream_peek` con datos nulos) rompe la cadena
explícitamente.

Un primer intento hizo la continuidad **dentro de una vuelta** del bucle de
drenaje, con la idea de que los paquetes de una misma vuelta son contiguos. Es
cierto pero casi no ocurre: los pares largo/corto llegan en callbacks distintos
separados por microsegundos. Quitó los picos mayores y nada más. La sujeción tiene
que cruzar callbacks.

## Lo que mide

```
antes:   perfil 0.0 -0.4 -0.0 -0.3 -0.4  5.7 -0.5 -0.4 -0.4 -0.4   max 19.71  saltos>5ms 47
después: perfil 0.0 -0.5 -0.5 -0.5 -0.5 -0.6 -0.6 -0.6 -0.7 -0.8   max  0.00  saltos>5ms  0
         forma: plano: ni desfase ni deriva
```

El «después» es sobre **60 s**, no 12: la sujeción podría acumular retraso si la
fuente fuera lenta, y había que ver si soltaba un salto de 40 ms al cabo de un
rato. No lo hace — la deriva se queda entre -0,5 y -0,8 ms durante todo el minuto.
Además la fuente va ligeramente rápida (+0,16 %), que es la dirección en la que la
sujeción ni siquiera actúa.

Re-anclas, sobre el mismo tono:

```
antes:    1, 0, 1
despues:  0, 0, 0
```

Y en la interfaz, `huecos 1` -> `huecos 0`.

## Estado

`ctest` verde en Windows y en Linux. `probes/linux-tono.sh` y
`probes/linux-interfaz.sh` en verde, este último ya con `huecos 0`. El CLI imprime
ahora las re-anclas junto a las discontinuidades: son cosas distintas y confundirlas
costó una comparación que parecía hecha y no lo estaba.

Sigue abierto de `docs/28`: el mutex sin destruir (`3/2`), PipeWire sin comprobar,
y la captura por aplicación en Linux sin prueba automática.
