# Las pérdidas, el baile de la onda y el tema

Fecha: 2026-08-24. Continúa [`04-interfaz.md`](04-interfaz.md).

## Las pérdidas no tienen nada que ver con la compresión

Pregunta razonable, respuesta corta: **no, y no se pueden resolver.**

Las pérdidas de docs/03 (unos 12.7 ms cada vez que la aplicación objetivo abre o
cierra su flujo de render) ocurren **dentro de WASAPI, al capturar**, antes de
que el motor haya tocado nada. No son un artefacto de guardar, ni de comprimir,
ni del buffer circular: son frames que el sistema operativo nunca entrega. No
hay API para recuperarlos.

La compresión, además, es **sin pérdida y exacta byte a byte** — está verificado
en `ssb selftest` con silencio, constantes, rampa, tono, ruido blanco y extremos
alternos, todos con round-trip idéntico. Encenderla o apagarla no cambia una sola
muestra del audio; solo cambia cuánto ocupa.

Lo que sí está resuelto, y es lo que importa, es que **el hueco no descoloca
nada**: la línea de tiempo la marca el reloj del sistema, así que el hueco se
representa donde de verdad ocurrió y se rellena con silencio al exportar. Las
pistas siguen alineadas entre sí (medido: 0.40 ms de diferencia máxima sobre tres
pistas). Y el audio que se pierde es justo el del instante en que la aplicación
arrancó o paró de sonar, que casi siempre es silencio.

Si en algún caso la continuidad importa más que el aislamiento, hay una salida
real: **grabar la salida del sistema en vez de la aplicación**. El loopback de
dispositivo no sufre estas pérdidas (medido: +0.037 % de desviación, sin
déficit), porque no depende de que la app abra y cierre flujos. Se pierde el
aislamiento por aplicación, se gana continuidad perfecta.

### El interruptor de compresión

Aun así se ha expuesto, porque tener el control es razonable:

- En la interfaz, desplegable **Guardar: Sin pérdida / Sin comprimir**, junto al
  del buffer. Se aplica a las pistas que se añadan a partir de ese momento.
- En el CLI, `--no-compress`.

Sin comprimir, cada pista ocupa unas 3 veces más en material con audio y unas
2340 veces más en silencio. No mejora nada de las pérdidas.

## El baile de la onda: un defecto real

La parte ya grabada de la onda se redibujaba y parecía ciclar en cada repintado.
No era un efecto óptico.

`i_envelope` repartía los picos devueltos **entre todas las columnas, por
índice**, dando por hecho que los picos cubrían exactamente el tramo pedido. No
lo cubrían, por dos motivos a la vez:

1. El anillo de picos se dimensionaba para `max_seconds`, pero el de audio guarda
   `max_seconds` **más un segmento entero** (un 12.5 % más). Así que la interfaz
   recibía menos picos de los que abarcaba el audio, y los estiraba.
2. La proporción índice→columna cambiaba en cuanto se descartaba un pico o
   entraba uno nuevo, así que **las posiciones de los picos viejos se movían un
   poco en cada frame**. Eso es lo que se veía.

Y había un tercer problema latente, del mismo linaje que el de docs/03: la
posición de un pico se deducía contando picos desde el ancla. Si la fuente pierde
frames, el mapa de picos se desplaza respecto del audio.

**Corregido de raíz: cada pico lleva su propio instante**, tomado del reloj del
sistema igual que los bloques (`ssb_peak.t`). `ssb_track_peaks` filtra por tiempo
real y la interfaz coloca cada pico en su columna por su instante, no por su
posición en el array. El anillo de picos, además, se dimensiona ahora con un 25 %
de holgura para cubrir lo mismo que el de audio.

### Verificado

`probes/run-gui-estabilidad.ps1` toma varias capturas separadas 600 ms y las
compara píxel a píxel. En el cuerpo de la onda de una pista:

```
columnas que cambian: x=381..410  (30 columnas)
a la izquierda de x=381: 0 pixeles distintos
```

El borde vivo, con 19 s grabados de un buffer de 60 s, cae en x≈385. O sea: solo
cambia donde se está grabando, y **todo lo anterior es idéntico píxel a píxel**
tras más de un segundo. Antes, las columnas cambiaban por toda la onda.

La lección, otra vez la misma: **colocar por tiempo, nunca por conteo.** Es el
tercer defecto de este proyecto con la misma raíz (los bloques en docs/03, la
envolvente por franja en docs/04, y ahora los picos).

## Tamaños de buffer

Nueve tamaños en vez de cuatro: 10 s, 30 s, 1 min, 2 min, 5 min, 15 min, 30 min,
1 h y 2 h. Los cortos importan más de lo que parece: con 10 s el circular se ve
descartar en diez segundos, así que se puede probar el comportamiento de régimen
sin esperar un minuto.

## El tema, ahora también en la ventana

Antes solo el lienzo seguía el tema. Ahora:

- **Fondo de la ventana y de las dos filas de la barra**, con `layout_bgcolor`.
- **Etiquetas** de la barra, con `label_color` y `label_bgcolor`.
- **Barra de título**, con `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)`.
  La barra de título la pinta el gestor de ventanas, no el toolkit, así que hay
  que pedírsela al sistema. Es la única línea específica de Windows de toda la
  interfaz, y está aislada en `i_dark_titlebar()`.

**Lo que no sigue el tema: los botones y desplegables.** NAppGUI no tematiza los
controles nativos en Windows — no hay una sola llamada a `SetWindowTheme` con
`DarkMode_Explorer` ni a `AllowDarkModeForWindow` en todo el backend. Los pinta
el sistema con su tema propio y se quedan claros. No es algo que se pueda
arreglar desde el consumidor sin API no documentada; sería una tarea del SDK.

Todo el tema se resuelve con `gui_alt_color(claro, oscuro)` y se recalcula en
caliente desde `gui_OnThemeChanged`. `--theme light|dark|system` lo fuerza.


## El tirón del borde izquierdo

El borde derecho avanzaba continuo y el izquierdo pegaba tirones: a veces se
quedaba quieto acumulando y de pronto saltaba. Dos causas, las dos reales.

1. **La granularidad del descarte.** El circular tira un segmento entero de
   golpe, y el segmento duraba `max_seconds/8`, o sea el 12.5 % del buffer. Ese
   era el tamaño del salto.
2. **La ventana se ataba al contenido real del anillo.** Como el anillo, tras
   descartar, se quedaba con *menos* de lo pedido, la ventana encogía y crecía
   con él.

Corregido por los dos lados:

- El segmento pasa a durar `max_seconds/16`, acotado entre medio segundo y
  30 s.
- **El anillo conserva ahora siempre al menos lo pedido**: descarta cuando el
  tramo pasa de `max_seconds` *más un segmento*, no de `max_seconds`. Antes
  oscilaba entre `max_seconds - segmento` y `max_seconds`; ahora entre
  `max_seconds` y `max_seconds + segmento`.
- Con esa garantía, la ventana vale **exactamente** el buffer nominal y va
  anclada al ahora. El borde izquierdo queda a distancia fija del derecho: los
  dos se mueven a la par por construcción.

Verificado con `probes/run-gui-pausa.ps1` sobre un buffer de 10 s en régimen,
midiendo por correlación cruzada el desplazamiento entre capturas consecutivas:

```
0->1: 57 px   1->2: 58 px   2->3: 48 px   3->4: 58 px   4->5: 57 px
media 55.6 px, desviacion 3.83 px   (correlaciones 0.97-0.98)
```

Un descarte de segmento con el código viejo habría dado un salto de ~140 px de
una vez y ceros en el resto. La prueba de regresión del selftest se endureció
para fijar el contrato nuevo: *el buffer nunca se queda corto*.

## "Pausar vista" no hacía nada, y no era lo que hacía falta

Dos problemas distintos, y el segundo importaba más.

**No funcionaba.** `wave_clamp` recalculaba la ventana en cada frame sin mirar
el estado de congelación: comprobaba `frozen` para una cosa y luego la
sobrescribía con otra. Ahora, congelada la vista, no se toca nada de la ventana.
Medido: 0 px de desplazamiento en 1.6 s, cuando sin congelar serían ~180 px.

**Y aun funcionando, no resolvía el problema de fondo**, que el usuario detectó
antes de que pasara: congelar la vista deja el circular corriendo, así que
mientras miras con calma lo que querías guardar se sale del buffer por atrás.
Se pierde información en vivo, exactamente como sospechaba.

La solución no es clonar el buffer —copiar minutos de audio para mirar un tramo
es caro y no hace falta— sino **detener la entrada**:

`ssb_track_pause()` marca la pista y el callback de captura descarta los
paquetes que llegan. **El cliente WASAPI se deja abierto a propósito**: reabrirlo
cuesta más de un segundo (hallazgo H2), y así reanudar es instantáneo. El buffer
queda intacto, byte a byte, y se puede mirar y guardar sin prisa. Al reanudar
queda un hueco real en la línea de tiempo, y como tal se representa.

En la interfaz es el botón **"Detener captura"** (Ctrl+P), separado de
**"Pausar vista"** (Ctrl+E). Verificado: con la entrada detenida, **0 columnas
del cuerpo de la onda cambian en 3 segundos**.

El aviso del HUD también se corrigió: antes decía "la grabación continúa" como
si fuera tranquilizador, cuando es justo lo contrario. Ahora dice "se sigue
grabando y lo viejo se irá perdiendo".
