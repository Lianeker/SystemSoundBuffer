# El motor: qué está hecho y qué se midió

Fecha: 2026-08-24. Continúa [`01-viabilidad-y-decisiones.md`](01-viabilidad-y-decisiones.md).

## Qué hay

`ssb_core`, librería C sin ninguna dependencia de GUI (D1), más un ejecutable
`ssb` que la ejercita entera desde línea de comandos. Compila con `/W4 /WX`:
**cero avisos, y los avisos son errores**.

```
ssb_core/
  include/ssb.h                 API pública
  src/ssb_codec.c               compresión sin pérdida por bloques
  src/ssb_ring.c                buffer circular en disco por segmentos
  src/ssb_track.c               pista: captura -> códec -> ring + picos
  src/ssb_wav.c                 escritura de WAV
  src/ssb_util.c                reloj, hilos, mutex, directorios (portable)
  src/win/ssb_capture_win.cpp   WASAPI (único fichero C++, por COM)
  tools/ssb_cli.c               CLI y autopruebas
```

```
ssb list
ssb rec --secs 20 --buffer 300 --src output --src app:WhatsApp --src input
ssb selftest
```

## Compresión: lo que consigue de verdad

Las autopruebas cubren los casos límite del códec, todos con round-trip
**idéntico byte a byte**:

| Material | Ratio |
|---|---|
| silencio digital | **2340:1** |
| constante distinta de cero | 2340:1 |
| extremos alternos (±32767) | 2340:1 |
| rampa | 31.6:1 |
| tono periódico | 1.37:1 |
| ruido blanco (peor caso) | 1.00:1 |
| ruido de baja amplitud, mono | 1.22:1 |

El escape de bloque constante que quedaba pendiente en docs/01 §5 está hecho, y
cumple: el silencio pasó de 15.94:1 a **2340:1**. El caso del ruido blanco
confirma el otro escape, el verbatim: comprimir nunca agranda.

## Cuatro pistas en vivo

`probes/run-engine.ps1`: cuatro fuentes simultáneas, buffer de 8 s con 14 s de
grabación para que el circular descarte en caliente.

| Pista | Fuente | Ratio | En disco | Buffer |
|---|---|---|---|---|
| 0 | salida del sistema | 3.13:1 | 884 KB | 7.25 s |
| 1 | **WhatsApp** (callado) | **2340:1** | **1.2 KB** | 7.34 s |
| 2 | una app emitiendo | 2.28:1 | 1209 KB | 7.17 s |
| 3 | micrófono (sala en silencio) | 7.20:1 | 183 KB | 7.51 s |

88 bloques descartados por pista en vivo, 8-9 segmentos, buffer estable en
~7.3 s de los 8 pedidos. Los cuatro WAV se validaron con el módulo `wave` de
Python: formato correcto, duración correcta y contenido correcto (la pista de
WhatsApp sale como silencio exacto, que es lo que estaba sonando).

**La cifra que importa para el objetivo:** la pista de WhatsApp costó 1.2 KB por
14.86 s = **291 KB por hora**. Un buffer de 4 horas de WhatsApp mayormente
callado cabe en algo más de un mega. El objetivo de "buffers de horas" no es que
se cumpla: sobra.

## Un defecto encontrado y corregido

La primera prueba en vivo reportó "184 bloques vivos, 15.7 s capturados, buffer
cubre 2.30 s". No cerraba, y no cerraba por una razón real:

El índice del ring se dimensionaba para `max_seconds` (8 s → 157 entradas), pero
todo el audio cayó en **un solo segmento de 4 MB**, y el segmento en curso no se
puede descartar. Así que `count` pasó de `cap` y el índice circular empezó a
sobrescribirse a sí mismo, devolviendo tramos sin sentido.

Dos defectos en uno:

1. **El índice podía desbordar.** Ahora hay una red de seguridad en
   `ssb_ring_append`: si el presupuesto no consiguió liberar sitio, se suelta el
   bloque más viejo antes que pisar nada.
2. **El límite de duración era inaplicable** cuando un segmento podía durar más
   que el buffer entero. Ahora el segmento rota también por tiempo, con un techo
   de `max_seconds/8`, así que la granularidad del descarte queda en el 12 % del
   buffer.

Hay una prueba de regresión que falla con el código viejo y pasa con el nuevo:
400 bloques (34 s) en un buffer de 8 s con segmentos de 4 MB. Cubre 7.85 s.

La lección para el resto del proyecto: **cualquier límite expresado en tiempo hay
que comprobarlo con la granularidad de descarte más grande posible**, no con la
cómoda.

> **Actualizado 2026-08-24:** los 80 ms de esta sección ya están diagnosticados y
> corregidos. No eran deriva de reloj. Ver [`03-sincronizacion.md`](03-sincronizacion.md).
> Lo que sigue se deja como quedó registrado en su momento.

## Lo que quedó instrumentado, no resuelto

`ssb_track_stats` reporta `drift_ms`: la diferencia entre el instante que reporta
el dispositivo y el que sale de contar frames. En la prueba en vivo:

- salida del sistema: 0.45 ms
- micrófono: 0.26 ms
- **loopback por proceso: 80 ms** sobre 15 s

Los dos primeros son ruido. El tercero **no**: 80 ms en 15 s es 0.53 %, dos
órdenes de magnitud más de lo que deriva un cristal. Eso no es deriva de reloj,
es un desfase sistemático del loopback por proceso — probablemente latencia de
arranque del flujo. Hay que averiguarlo antes de mezclar esa pista con otras,
porque 80 ms de desfase entre la voz del otro y la tuya se oyen.

Es el riesgo nº1 de docs/01 §8, ahora con instrumentación y un número.

## Decisiones de implementación que no estaban en docs/01

**El hilo de captura hace todo el camino caliente**: convertir, acumular,
comprimir y escribir a disco. Se puede permitir porque el cliente WASAPI tiene
200 ms de buffer y un bloque son 85 ms de audio que se comprimen en decenas de
microsegundos. Si algún día no alcanza, la cola va entre el acumulador y el
compresor, no antes.

~~**Los huecos se rellenan con silencio** en el hilo de captura, cuando el
instante reportado se adelanta más de 100 ms de la cuenta de frames.~~
**Revertido el mismo día.** Era un parche sobre un diagnóstico equivocado, y de
paso hacía ilegible la medida que hacía falta para diagnosticarlo bien. Ahora la
línea de tiempo la marca el reloj del sistema y el hueco lo rellena quien
exporta. Ver [`03-sincronizacion.md`](03-sincronizacion.md).

**El bloque a medias se cierra antes de guardar.** `ssb_track_save_wav` fuerza el
volcado del acumulador, para que el tramo pedido incluya lo último capturado y no
se pierdan hasta 85 ms al final.

## Siguiente

1. ~~Investigar los 80 ms del loopback por proceso.~~ Hecho: docs/03.
2. Captura en Linux (`ssb_core/src/linux/`), con la misma interfaz interna.
3. La interfaz con NAppGUI, consumiendo `ssb_track_peaks` para la onda.
