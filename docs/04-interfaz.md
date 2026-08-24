# La interfaz

Fecha: 2026-08-24. Continúa [`02-motor.md`](02-motor.md).

## Qué hay

`ssbgui`, construida con NAppGUI consumido por `find_package` desde el prefijo de
instalación del proyecto de al lado. Consume `ssb_core` y no sabe nada de WASAPI
(decisión D1). Dos ficheros: `ssbapp.c` (ventana, controles, ciclo de vida) y
`ssbwave.c` (el lienzo de ondas).

- Selector de fuente con todo lo que enumera el motor: dispositivos de salida, de
  entrada, y aplicaciones con sesión de audio.
- Añadir y quitar pistas, con la duración del buffer elegible antes de crearlas.
- Lienzo multipista con una banda por pista, dibujado **del mapa de picos**, no
  del audio: por eso responde igual con un buffer de un minuto que de dos horas.
- Selección de un tramo arrastrando, zoom con la rueda, desplazamiento con el
  botón derecho, y "ir al directo" para volver a pegarse al borde vivo.
- Pausar la vista sin parar la grabación (D7).
- Guardar la selección de todas las pistas, con diálogo o con Ctrl+S al
  directorio del buffer.
- Arranque con fuentes preconfiguradas: `ssbgui --src output --src app:WhatsApp`.

El refresco va por `osmain_sync(0.04, ...)`, que llama a `i_update` **en el hilo
de interfaz** 25 veces por segundo. Los hilos de captura son del motor y la
interfaz no los toca: solo lee bajo el mutex de cada pista.

## Verificado, no supuesto

Con tres fuentes reales grabando a la vez (salida del sistema, una aplicación y
el micrófono), automatizando ratón y teclado sobre la ventana:

- **Mapeo tiempo↔píxel.** Un arrastre de 560 px sobre un lienzo de 1408 px con
  una ventana de 30 s dio "Selección: 11.93 s". 560/1408×30 = 11.93. Exacto.
- **Guardado con Ctrl+S.** Escribe un WAV por pista, todos válidos y con audio
  real (comprobado con el módulo `wave` de Python).
- **Alineación multipista.** Las tres pistas salieron con 258615, 258630 y 258611
  frames: **0.40 ms de diferencia máxima**. Es la garantía que hacía falta.

`probes/run-gui.ps1` arranca la aplicación, la deja grabar y captura la ventana.
`probes/run-gui-select.ps1` hace además el arrastre y el Ctrl+S, y comprueba los
ficheros resultantes.

## Un defecto encontrado al probar

Las tres pistas salían con duraciones distintas: 6.00, 5.99 y **5.28 s**. La
causa era que cada pista se recortaba contra **su propio** buffer, y el
micrófono había empezado más tarde — el hallazgo H2 de docs/01 (abrir un
dispositivo de entrada tarda más de un segundo) apareciendo en la práctica.

Corregido: el guardado recorta contra la **ventana común** a todas las pistas,
igual que ya hacía el CLI. Sin eso, las pistas no se pueden montar juntas, que es
el motivo entero de la aplicación.

## El modelo de la ventana de tiempo

**El ancho del lienzo es el buffer.** No es una metáfora: la ventana visible vale
exactamente lo que el buffer contiene, y no se puede mirar ni seleccionar nada
fuera de él.

- **Mientras se llena**, la ventana vale el buffer nominal y está anclada al
  principio: la onda crece de izquierda a derecha, y lo que queda por llenar se
  dibuja con un tono distinto, no como silencio. Son dos cosas diferentes y
  ahora se distinguen.
- **En régimen**, la ventana vale lo que el buffer contiene de verdad y está
  anclada al ahora: la onda ocupa todo el ancho y se traslada hacia atrás.

El corte entre los dos casos **no** es "ya cabe en la ventana" sino "el circular
ya ha descartado algo" (`app_filling()`). Importa: un buffer de 1 min se
estabiliza alrededor de 56 s por la granularidad del descarte por segmentos, así
que comparar contra el nominal dejaba para siempre una franja vacía a la derecha
que no significaba nada. Verificado: llenándose, `buffer 26.3 s / ventana 60.0 s`;
en régimen, `buffer 56.6 s / ventana 56.6 s`.

La rueda hace zoom dentro de esos límites y marca la ventana como "del usuario";
alejarse más allá del buffer no hace nada porque no hay nada que enseñar. "Ir al
directo" devuelve el control automático. La selección se recorta al buffer al
arrastrar, así que lo que dice el HUD es lo que se va a guardar.

## Tema claro y oscuro

El lienzo sigue el tema del sistema con `gui_alt_color(claro, oscuro)`, y
`gui_OnThemeChanged` recalcula la paleta y los colores de pista en caliente si el
sistema cambia mientras la aplicación está abierta. Todos los colores salen de
una `Palette` que se rellena en un único sitio (`wave_palette`).

`--theme light|dark|system` fuerza uno de los dos, para quien prefiera lo
contrario que su escritorio y para poder verificar ambos sin tocar la
configuración de Windows.

## Una investigación que acabó en falso positivo

Durante el desarrollo llegué a documentar que "la vista se cree 1.244× más alta
que su área visible" y a acusar de ello al SDK. **Era falso, y el error era
mío.** El script de captura era DPI-unaware: `GetWindowRect` le devolvía
coordenadas virtualizadas mientras `CopyFromScreen` capturaba píxeles físicos, o
sea que yo recortaba el 80 % superior izquierdo de la ventana y medía contra la
anchura equivocada. Con `SetProcessDPIAware()` en el script, lo calculado y lo
renderizado coinciden: el programa situaba el borde de los datos en el 33.1 % y
se mide en el 33.7 %, dentro del error de detección de bordes.

De paso, `osview_scale_factor()` devuelve 1 en el backend de Windows. Eso es
coherente con una aplicación que no declara conciencia de DPI, y **no** es el
defecto que llegué a suponer.

Los cambios que hice creyendo que arreglaba algo (regla y estado arriba, lienzo
desplazable) se quedan porque valen por sí solos: con ocho pistas hace falta
scroll de todas formas. Pero la lección es la de siempre y me la salté: **antes
de acusar al código ajeno, hay que validar el instrumento de medida.**

## Un defecto real que sí salió de ahí

El diagnóstico que monté para investigar aquello imprimió
`pista 0 from 1844674407370.9` — un desbordamiento. `ssb_time` es `uint64_t`, y
`wave_time_to_x` hacía `t - from` sin signo: para un instante anterior al borde
izquierdo de la ventana, que es lo normal en cuanto el buffer se llena, el
resultado no era una `x` negativa sino 1.8e19. El sombreado de la banda lo usaba
para decidir cuánto pintar de "vacío". Corregido con resta con signo y recorte
por los dos lados.

## Detalles que costaron un rato

- **`nappgui.h` no arrastra `osapp/osmain.h`.** La macro del punto de entrada se
  incluye junto a su uso; es la convención del SDK (`demo/drawbig/drawbig.c:981`,
  `test/consumer/main.c:144`). No es un defecto, es cómo se usa.
- **`find_package(nappgui)` va antes de crear ningún target.** El paquete fija
  `CMAKE_MSVC_RUNTIME_LIBRARY` para que el consumidor use el mismo CRT que el
  SDK; llamándolo después, los targets ya creados se quedan con el CRT por
  omisión y el enlace falla con `__imp_fwrite` sin resolver.
- **El SDK instalado hoy solo exporta Debug.** La interfaz se compila en Debug;
  en otras configuraciones el CMake la omite con un mensaje claro en vez de
  romper el build. Para Release, instalar antes el SDK en Release.

## Lo que falta

- Reordenar pistas y quitar una que no sea la última.
- Reproducir la selección antes de guardarla.
- Recordar las pistas y el tema entre sesiones.
- Captura en Linux, que es lo único que falta para que la aplicación sea de
  verdad multiplataforma.
