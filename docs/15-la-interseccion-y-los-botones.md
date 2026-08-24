# 15 — La intersección, y hasta dónde llega NAppGUI

## 1. Añadir una pista borraba el pasado

Síntoma: añades una pista a mitad de la grabación y ya no puedes seleccionar
nada anterior a ella; si tenías algo seleccionado, reproducirlo responde *«that
selection falls outside what every track covers»*.

Los dos síntomas eran el mismo defecto, y el mensaje —aunque literal— no
apuntaba a la causa.

`app_span` devolvía la **intersección** de lo que cubre cada pista. Una pista
recién creada cubre desde *ahora*, así que la intersección se colapsaba a
*ahora*. Todo lo anterior dejaba de existir para la interfaz.

La intersección estaba ahí por una razón real: que al exportar todas las pistas
durasen lo mismo. `ssb_track_save_wav` recortaba el tramo pedido al tramo de
cada pista, así que una pista corta producía un fichero corto, y las pistas
dejaban de estar alineadas entre sí (era el hallazgo H2 de `docs/01`).

**Se arregló donde estaba el problema, no donde se veía.** Ahora la exportación
escribe el tramo pedido **entero** y rellena con silencio lo que una pista no
cubra — que es literalmente lo que esa pista estaba grabando en ese rato: nada.
El mecanismo de relleno ya existía (`i_pad_until`, de `docs/12`); solo había que
dejar de recortar y aceptar que «este anillo no tiene nada aquí» no es un error.

Con eso, la interfaz puede ofrecer la **unión** sin que nada se desalinee.

### Medido

`probes/run-pista-tarde.ps1`: graba 12 s, añade una segunda pista en marcha,
graba 10 s más, selecciona todo y exporta.

```
seleccionable: 22.19 s (union de todas)
1  Headphones (...)   cubre 22.19 s de 120  huecos 0
2  powershell.exe     cubre 10.09 s de 120  huecos 0

2026-08-24_17-06-23-1.wav  22.19 s  primer sonido t= 0.00 s
2026-08-24_17-06-23-2.wav  22.19 s  primer sonido t=12.12 s
```

Las dos duran exactamente lo mismo, y el audio de la pista tardía empieza en
t=12.12 s: donde se añadió. El silencio de delante es correcto, no relleno de
emergencia.

De paso, `tracks` dice ahora **qué tramo cubre cada pista** y cuánto es
seleccionable en total. Eso es lo que habría explicado el fallo de un vistazo.

Comprobado además que no se han reintroducido artefactos: 0 interrupciones en
51 s de audio en dos fuentes.

## 2. Hasta dónde llega NAppGUI para personalizar

La pregunta era si se puede dar estilo a los botones y al tema oscuro, o si hay
que hacer botones propios. Mirando la API en vez de suponer:

```
$ grep '_gui_api void button_' nappgui_src/src/gui/button.h
button_text  button_text_alt  button_tooltip  button_font
button_image  button_image_alt  button_image_pos
button_state  button_tag  button_hpadding  button_vpadding
```

- **No hay color de fondo ni de texto.** Debajo hay un control nativo del
  sistema, y NAppGUI no expone forma de repintarlo. Un `▶` de texto saldría del
  color que decida Windows, y un círculo de grabación que no es rojo no es un
  círculo de grabación.
- **Sí hay `button_image`.** Un botón nativo acepta un bitmap, y ahí el color es
  nuestro por completo.
- **El tema oscuro ya funciona** sin hacer nada especial: los controles nativos
  siguen al del sistema, `gui_alt_color()` resuelve los colores del lienzo y la
  barra de título se fuerza por DWM (eso último sí hubo que hacerlo a mano, y
  está anotado como `NAP-042` en el backlog del SDK).
- **Botones propios: se puede, y ya se hace.** Los botones **M** y **X** de cada
  banda los dibujamos nosotros dentro del `View`, porque un control nativo no se
  puede meter dentro de un lienzo ni desplazarse con la lista.

Así que la respuesta es: **para los símbolos de transporte no hace falta control
propio**; basta con darle al botón nativo una imagen generada. `ssbicon.c` crea
los cuatro iconos por código (círculo, triángulo, cuadrado, pausa) en RGBA con
supermuestreo 4x4 para que los bordes no se vean como escaleras al lado de los
controles del sistema. La transparencia es proporcional a la cobertura, así que
el icono se funde con el fondo del botón sea claro u oscuro sin tener que saber
cuál es.

Colores: rojo para grabar, verde para reproducir, ámbar para pausa, gris neutro
para parar. No es decoración — es lo que permite reconocer el botón sin leerlo.
El texto se queda al lado a propósito: el símbolo se identifica de un vistazo y
el texto quita cualquier duda de qué va a pasar al pulsar.

### Y un detalle que costó una captura

El círculo rojo no aparecía. La imagen se asignaba en `app_relabel`, que al
arrancar corre **antes** de que el layout calcule el tamaño del botón: sin
imagen en ese momento, no se reserva sitio para ella, y ponerla después la deja
invisible hasta que algo fuerza un recálculo. El síntoma era que el círculo solo
salía después de pulsar Grabar una vez.

La imagen se asigna ahora también al crear el botón, junto al texto — que es
donde ya estaba el mismo cuidado con las etiquetas, por el mismo motivo
(`docs/04`: los botones salían todos del mismo tamaño si se rotulaban tarde).

## 3. Herramienta nueva: `probes/foto.ps1`

Ejecuta un guion en la aplicación y fotografía la ventana con **PrintWindow**.
Reutilizable para cualquier comprobación visual, y sin el problema de
`CopyFromScreen`, que fotografía lo que haya encima — en `docs/12` acabó
capturando una conversación ajena que se puso delante.
