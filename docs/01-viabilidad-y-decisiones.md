# SystemSoundBuffer — viabilidad y decisiones

Fecha: 2026-08-24. Todo lo que sigue está **medido en esta máquina** (Windows 11
26100, MSVC 14.50, Windows SDK 10.0.26100) o citado como `ruta:línea`. Lo que no
está verificado va marcado como hueco abierto.

## 1. Qué es

Grabadora continua de audio. Varias fuentes a la vez, cada una en su pista, todas
escribiendo a un buffer circular de duración configurable. Pausar, seleccionar un
tramo de la línea de tiempo, guardarlo, y seguir grabando sin cortar.

### Modelo de fuentes y pistas

Una **fuente** es una de estas tres cosas, y de cada tipo puede haber varias:

1. **Un dispositivo de salida**, capturado por loopback: todo lo que suena por
   ese dispositivo. Si hay altavoces, HDMI y unos auriculares Bluetooth, son tres
   fuentes distintas y se pueden grabar por separado.
2. **Una aplicación concreta** (WhatsApp, un navegador, un juego). El loopback
   por proceso no está atado a un dispositivo: sigue al proceso.
3. **Un dispositivo de entrada**: micrófono, línea de entrada.

Una **pista** es un carril de grabación al que el usuario le asigna una fuente.
El usuario añade y quita pistas, y elige qué fuente va en cada una. Ver D9 para
la cardinalidad.

## 2. Veredicto: viable, y verificado

`probes/multi.cpp` abre cuatro clientes WASAPI **simultáneos**, cada uno en su
hilo. Compilado con `cl /W4` sin un solo aviso. Resultado real:

| Fuente | Formato entregado | Capturado en 4 s | Pico |
|---|---|---|---|
| Salida del sistema (loopback de dispositivo) | 2ch 48 kHz float32 | 3.979 s | 0.0592 (mezcla) |
| App A (loopback por proceso) | 2ch 48 kHz float32 | 3.947 s | 0.3562 |
| App B (loopback por proceso) | 2ch 48 kHz float32 | 3.947 s | 0.1739 |
| Micrófono (dispositivo de entrada) | 1ch 48 kHz float32 | 4.000 s | 0.0174 |

Picos distintos en A y B con las cuatro fuentes abiertas = **cada pista aísla su
aplicación de verdad**. Cero discontinuidades en los loopbacks por proceso.

`probes/probe.cpp` aísla los casos uno a uno y añade la prueba de control: en
modo `EXCLUDE_TREE` sobre el PID emisor, el pico cae a **0.0000**. El aislamiento
no es cosmético.

WhatsApp en esta máquina es la app MSIX `5319275A.WhatsAppDesktop`, cuya raíz de
árbol de procesos es `WhatsApp.Root.exe`. Con
`PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE` sobre su PID: activación OK,
`Initialize` OK, 2.99 s de frames continuos. Pico 0.0 porque no estaba emitiendo.

## 3. Hallazgos que condicionan el diseño

**H1. El loopback por proceso acepta float32 a 48 kHz.** No hay resampleo
forzado: las cuatro fuentes entregan 48 kHz float32 nativo (el micrófono, mono).
Elimina el resampler del MVP. Solo reaparece si algún dispositivo corre a
44.1 kHz.

**H2. Abrir un dispositivo de entrada tarda más de un segundo.** El micrófono
apareció con `primer qpc = +1448 ms` y `+868 ms` en dos corridas, mientras los
loopbacks arrancaron con ~40 ms de diferencia entre sí. Consecuencia: se abren
todas las fuentes al inicio y se dejan corriendo. Nunca abrir una fuente por
demanda.

**H3. Las pistas NO empiezan juntas.** Hay que colocarlas por `qpcPosition` (el
5º parámetro de `IAudioCaptureClient::GetBuffer`), que es QPC en unidades de
100 ns y sí es un reloj común a todas las fuentes. Asumir "todas empiezan a la
vez" produce pistas desfasadas más de un segundo.

**H4. Cada fuente vive en su dominio de reloj.** Los loopbacks siguen el reloj
del dispositivo de salida; el micrófono el del suyo. En 4 s no se nota; en una
hora, sí. Hay que corregir deriva comparando frames acumulados contra QPC.

**H5. El PID cambia al reiniciar la app.** Resolver la fuente por nombre de
proceso y reactivar.

**H6. El loopback por proceso entrega frames de silencio continuos** cuando la
app no suena. La línea de tiempo no tiene huecos, que es exactamente lo que
necesita un buffer circular.

**H7. El buffer va a estar mayoritariamente en silencio.** Un buffer de WhatsApp
pasa casi todo el tiempo callado. Esto no es un detalle: decide qué compresión
conviene (§5), porque un códec de ratio fijo cobra lo mismo por el silencio que
por la voz, y uno sin pérdida no.

## 4. Decisiones

**D1. El motor va separado de la GUI.** `ssb_core`: librería C sin una sola
dependencia de GUI, que expone fuentes, ring buffers, mapa de picos y guardado.
La GUI es un consumidor. Motivo: la elección de toolkit deja de ser una puerta de
un solo sentido.

**D2. La GUI se hace con NAppGUI.** Es event-driven (en reposo no repinta, y esto
graba todo el día), widgets nativos, cero dependencias, y el puente hilo→UI ya
existe. Ver §7 para el criterio y las alternativas descartadas.

**D3. Audio al disco, picos en RAM.** Un fichero circular por pista (con
`bfile_*`, que es portable) y en RAM solo el mapa de picos — min/max por bloque
de 256 muestras, ~1.5 KB/s/pista, o sea ~5 MB por hora y pista. La onda se dibuja
siempre del mapa de picos: la vista es instantánea y la RAM queda plana sea el
buffer de 5 minutos o de 5 horas.

**D4. Formato interno: 48 kHz. float32 en el camino caliente, int16 en el ring.**
float32 es lo que entregan todas las fuentes (H1), así que no hay conversión al
capturar; el ring guarda int16, que es 2:1 gratis porque el material subyacente
casi nunca tiene más de 16 bits reales.

**D5. La línea de tiempo maestra es QPC, y manda sobre la cuenta de frames.**
Cada bloque lleva el QPC **reportado** de su primer frame; nunca uno calculado a
partir de cuántos frames se llevan recibidos. No es un matiz: hay fuentes que
pierden frames (docs/03), y contarlos desplaza todo lo posterior. Los huecos los
rellena quien exporta, que es donde se sabe qué tramo se pide.

**D6. Una fuente = un hilo = un ring.** Sin estado compartido entre fuentes salvo
la línea de tiempo. Sincronización con `bmutex` (`src/osbs/bmutex.h`): el lock es
de microsegundos y el hilo de audio tiene ~10 ms de holgura.

**D7. Pausar congela la vista, no la captura.** Se sigue grabando siempre; pausar
solo detiene el avance del cursor de la vista. Así no se pierde nada de lo que
pasó mientras mirabas.

**D8. Compresión sin pérdida por bloques, configurable por pista.** Ver §5: es la
opción por omisión porque en material realista comprime **más** que ADPCM y no
pierde nada. Los modos con pérdida quedan como elección explícita del usuario
para pistas donde solo importa la duración.

**D9. Una pista = exactamente una fuente.** Mezclar varias fuentes en una pista
se resuelve al exportar, no al grabar. Motivo: mezclar en caliente obliga a
alinear y resamplear dos dominios de reloj (H4) en el camino crítico, y no aporta
nada que no se pueda hacer después. El usuario añade tantas pistas como fuentes
quiera seguir.

## 5. Compresión: medido, no estimado

`probes/squeeze.cpp` captura audio real y mide cada códec. Material de prueba:
11.98 s de la salida del sistema con **57.7 % de frames en silencio digital
exacto** — el perfil que de verdad va a tener el buffer (H7). Compilado `/W4 /O2`
sin avisos.

| Modo | Ratio | MB/min | 1 h/pista | Error máximo |
|---|---|---|---|---|
| crudo float32 48k estéreo | — | 23.0 | 1.35 GB | — |
| **int16 48k estéreo** (base del ring) | 2:1 | 11.0 | 660 MB | — |
| **sin pérdida, por bloques** | **5.63:1** | **1.95** | **117 MB** | **0** |
| µ-law 8 bits | 2:1 | 5.49 | 330 MB | 63/32768 |
| IMA ADPCM 4 bits | 4:1 | 2.75 | 165 MB | 192/32768 |
| voz: 16k mono + ADPCM *(aritmético, no medido)* | 24:1 | 0.46 | 27.5 MB | — |

**El sin pérdida gana.** 5.63:1 contra el 4:1 de ADPCM, y con error de
round-trip **exactamente 0** (verificado decodificando y comparando muestra a
muestra). El motivo es H7: por la mitad silenciosa cobró 15.94:1 y por la mitad
con audio 3.42:1, mientras ADPCM cobra 4:1 por las dos. Un códec de ratio fijo
paga el silencio a precio de música.

**Coste de CPU: irrelevante.** 33.5 MB/s comprimiendo, y ese número incluye el
round-trip de verificación dentro del cronómetro, así que es un suelo. Una pista
de 48k estéreo produce 0.192 MB/s: **0.6 % de un núcleo por pista**. Cuatro
pistas, 2.3 %.

### Formato

Decorrelación mid/side, predictor fijo de orden 0-3 elegido por bloque, y
codificación Rice con *k* elegido por bloque. Bloques de 4096 frames = 85 ms a
48 kHz.

**Los bloques independientes no son un detalle de implementación, son el
requisito.** "Seleccionar cualquier tramo y guardarlo" exige búsqueda aleatoria,
y un compresor de flujo la prohíbe. Cada bloque se decodifica solo, y el índice
en RAM (offset, QPC, nº de frames) es lo que permite saltar a cualquier punto.

> **Actualizado 2026-08-24:** la mejora que sigue ya está hecha y medida en el
> motor. El silencio pasó de 15.94:1 a **2340:1**. Ver docs/02.

### Mejora pendiente, y es grande

El 15.94:1 del silencio es un límite de **mi codificador de juguete**, no de la
teoría: Rice gasta un mínimo de ~2 bits por muestra incluso con residuo cero. Con
un escape de bloque constante (lo que FLAC llama subframe CONSTANT), un bloque de
4096 frames en silencio se codifica en unos pocos bytes. Para un buffer de
WhatsApp con 95 % de silencio eso debería llevar el ratio bastante más allá de
30:1. Es la primera optimización a hacer, y es unas 20 líneas.

### Presupuesto resultante

Con sin pérdida y 4 pistas: **468 MB por hora**, 1.9 GB por 4 horas. Con el
escape de silencio, mucho menos. En modo voz, 110 MB por hora. Buffers de
minutos sin comprimir y de horas comprimidos: confirmado con números.

Hay un segundo motivo para comprimir que no es la capacidad: **el desgaste del
SSD**. Cuatro pistas en int16 escriben 44 MB/min = 63 GB al día si esto corre
todo el tiempo. Con 5.63:1 son 11 GB al día.

## 6. Plataformas

Alcance: **Windows y Linux. macOS queda fuera** (decisión del usuario,
2026-08-24). Motivos que la respaldan: exige macOS 14.4+, el permiso de captura
es una categoría TCC propia sin API pública para pedirlo ni consultarlo (el
diálogo salta recién en `AudioDeviceStart`, o sea que hay que montar toda la
tubería para que el sistema pregunte), y los process taps **exigen identidad de
firma estable** — un build sin firmar no dispara el diálogo y no captura. Sin
cuenta de desarrollador de Apple no se puede ni probar.

La GUI es portable. **La captura son implementaciones nativas**, y ninguna
librería lo arregla: miniaudio tiene loopback solo en WASAPI, PortAudio y RtAudio
no tienen loopback, SDL tampoco. Por eso D1.

| | Salida del sistema | Por aplicación | Coste |
|---|---|---|---|
| **Windows** | `AUDCLNT_STREAMFLAGS_LOOPBACK` | `ActivateAudioInterfaceAsync` + `AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK` | **hecho y medido** |
| **Linux** | monitor del sink | monitor acotado a un sink-input (`pa_stream_set_monitor_stream`), o nodo PipeWire | medio |
| ~~macOS~~ | ~~Core Audio process tap~~ | ~~`CATapDescription`~~ | fuera de alcance |

**Linux.** Se ataca el monitor del sink acotado a un sink-input concreto: sin
null-sink, sin re-rutear la app, sin latencia añadida. Es la vía que usa
`obs-pulseaudio-app-capture` desde su v0.2, después de abandonar el truco del
combine-sink por inestable. `libpulse` sirve para las dos columnas, y PipeWire
nativo (enlazar al nodo) es la alternativa más capaz.

## 7. Por qué NAppGUI y no otra cosa

El argumento "cross-platform" no decide nada aquí, porque la parte que de verdad
cuesta portar (la captura) hay que escribirla por plataforma con cualquier
librería. Así que el toolkit se elige por otra cosa: cuán rápido se construye una
timeline multipista y cuán bien se dibuja.

- **NAppGUI** — elegido. Event-driven (crítico para una app siempre encendida),
  widgets y menús nativos, cero dependencias, binario mínimo, y `osapp_task` ya
  es el puente hilo-de-audio → UI que hace falta.
- **Dear ImGui** — mejor substrato si esto crece a editor multipista con zoom
  fluido sobre 16 pistas: dibuja por GPU y las timelines son su especialidad.
  Descartado por ahora: repinta continuamente (va contra una app de fondo), no
  tiene widgets ni menús nativos, y son ~100 h de UI en vez de ~20. Con D1,
  migrar después cuesta poco.
- **raylib** — descartado: framework de juegos, bucle siempre activo, sin widgets.
- **Qt** — descartado: lo contrario de liviano, complica despliegue y licencia.
- **FLTK** — alternativa razonable y minúscula, pero no aporta nada sobre NAppGUI
  y pierde el conocimiento y el andamiaje ya montados.

### Lo que NAppGUI aporta, verificado en los tres backends

| Pieza | Windows | macOS | Linux |
|---|---|---|---|
| Timer del bucle que dispara `osapp_task` | `SetTimer(...,20,...)` `osapp/win/osapp_win.c:197` | `NSTimer 0.01` `osapp/osx/osapp_osx.m:147` | `g_timeout_add(10,...)` `osapp/gtk/osapp_gtk.c:249` |
| Repintado | `InvalidateRect` `osgui/win/osview.cpp:610` | `setNeedsDisplay` `osgui/osx/osview.m:725` | `gtk_widget_queue_draw` `osgui/gtk/osview.c:642` |
| `draw_polyline` por lotes | GDI+ `DrawLines` `draw2d/win/draw_win.cpp:209` | path+stroke `draw2d/osx/draw_osx.m:318` | path+`cairo_stroke` `draw2d/gtk/draw_gtk.c:226` |

(La columna de macOS se deja como constancia de que el SDK no cojea ahí; el
alcance del proyecto sigue siendo Windows y Linux.)

`func_main` de `osapp_task` corre en un hilo propio (`bthread_create`,
`osapp/osapp.c:181`) y `func_update` se ejecuta **en el hilo de UI** cada
`updtime` s (0.04 por omisión = 25 Hz) desde ese timer (`osapp.c:224`). Es
exactamente el patrón que necesita la app, y ya está hecho.

Piezas de la UI: `view_custom()` (`gui/view.h:25`) con
`OnDraw/OnOverlay/OnDown/OnUp/OnMove/OnDrag/OnWheel`, vista virtual con scroll
por `view_content_size` + `view_viewport` (`gui/view.h:75,85`), y rasterizado
offscreen con `dctx_bitmap()` (`draw2d/dctx.h:21`) para que el coste por frame no
dependa del zoom. Para el WAV, `stm_to_file` + `stm_write_u32/u16/i16` +
`stm_set_write_endian` (`core/stream.h:27,39,99-109`).

Referencias a copiar del SDK: `demo/bode/bdplot.c` (widget de gráfica con
conversión ratón↔datos, `i_xy_from_canvas:263`, `i_eval_mouse:373`),
`demo/drawbig/drawbig.c:70,269` (vista virtual con scroll) y
`demo/hellocpp/main.cpp` (una app NAppGUI que es C++, necesario para el COM de
WASAPI).

## 8. Riesgos y huecos abiertos

1. ~~**Deriva de reloj entre pistas en grabaciones largas** (H4).~~ **CERRADO**
   2026-08-24. No era deriva de reloj: el loopback por proceso pierde ~12.7 ms de
   frames en cada apertura o cierre del flujo de la app objetivo. La línea de
   tiempo pasó a ser la que reporta el sistema y el desfase quedó constante
   (−5.3 ms, verificado por correlación cruzada sobre 60 s con 394 ms de frames
   perdidos). Ver [`03-sincronizacion.md`](03-sincronizacion.md).
2. **El ratio sin pérdida se midió sobre 12 s de un tono de alarma**, no sobre
   voz real de una llamada ni sobre música. El 5.63:1 es orientativo; el
   comportamiento por mitades (3.42 con audio, 15.94 en silencio) es el dato
   sólido. Hay que remedirlo con material real.
3. **El modo voz (24:1) es aritmética, no medición.** No hay resampler
   implementado ni evaluación de calidad. Si se promete, se mide primero.
4. **Techo de GDI+** con muchas pistas y zoom alto. Mitigado por D3 y por el
   rasterizado offscreen, pero sin medir.
5. **`pa_stream_set_monitor_stream` bajo pipewire-pulse**: no verificado si la
   capa de compatibilidad lo implementa entero. Si no, hay que ir a PipeWire
   nativo para la captura por aplicación.
6. **Límite de clientes WASAPI simultáneos**: probados 4. No se sabe dónde está
   el techo real ni qué pasa con 16 pistas.
7. **Ninguna demo del SDK ejercita "hilo de fondo + refresco continuo de UI".**
   `products` usa `osapp_task` una vez para un login (`prctrl.c:728`) y
   `fractals` usa `bthread` en modo bloqueante (`fractals.c:158-164`). El patrón
   está soportado, pero seríamos los primeros en estresarlo.
8. **Consentimiento.** Grabar una conversación en la que participás es legal en
   muchas jurisdicciones; grabar a terceros sin avisar no siempre. Decisión del
   usuario, pero la app debería dejar rastro claro de que está grabando.

## 9. Reproducir la evidencia

```powershell
. C:\Users\DeZero\Documents\Prog\Claude5\nappgui\tools\env.ps1
cd probes
cl /nologo /W4 /EHsc /std:c++17 probe.cpp   /Fe:probe.exe   /link ole32.lib mmdevapi.lib propsys.lib avrt.lib
cl /nologo /W4 /EHsc /std:c++17 multi.cpp   /Fe:multi.exe   /link ole32.lib mmdevapi.lib propsys.lib avrt.lib
cl /nologo /W4 /O2  /EHsc /std:c++17 squeeze.cpp /Fe:squeeze.exe /link ole32.lib mmdevapi.lib propsys.lib
.\run.ps1     # casos uno a uno, con el control de EXCLUDE
.\run2.ps1    # cuatro fuentes simultaneas, incluida WhatsApp
.\run3.ps1    # captura material real y mide cada codec
```

Los tres scripts hacen sonar WAVs del sistema: es la única forma de distinguir
"capturé silencio" de "no capturé nada".
