# 19 — La barra al estilo del explorador

Pediste botones minimalistas: cuadrados, sin borde, del color del fondo, que
solo cambien al pasar el ratón, y separadores prolijos. Eso es exactamente el
**botón plano** que NAppGUI ya tenía… y que no admitía texto.

## El bloqueo: NAP-045

`button_flat()` está pensado para barras de herramientas de iconos, así que
trata el texto como la **etiqueta emergente del icono**. Sin icono no queda nada
que dibujar: saltaba una aserción y, sin aserciones, el botón salía de tamaño
cero. Invisible.

La regla nueva cabe en una frase:

> Con icono, el texto es la etiqueta emergente. **Sin icono, el texto se
> dibuja.**

Al tirar del hilo salieron tres cosas más, y las tres hacían falta para que el
botón se viera de verdad:

**1. Holgura cero.** La holgura por omisión se sacaba de la imagen
(`imgwidth * .5`). Sin imagen eso da **cero** y los botones salían pegados:
`FileEditView`. Ahora, sin imagen, se saca del alto del texto — lo único que
escala con la fuente y con el DPI.

**2. Un COLORREF donde se esperaba un `color_t`.** `_osdrawctrl_gdi_text`
declaraba el parámetro como `COLORREF` pero lo consumía con `color_get_rgb`, y
ahí **un alfa a cero significa «color indexado»**. Un `COLORREF` crudo lleva ese
byte a cero, así que el color pedido se reinterpretaba y salía otro. O sea que
`button_color()` —lo que se añadió en NAP-044— **no pintaba nada en Windows**, y
no se había visto porque aquella demostración usaba iconos, no texto.

De paso, el centinela «usa el color del tema» era `UINT32_MAX`, que como
`color_t` es **el blanco opaco**: con ese centinela no había forma de pedir
texto blanco. Ahora es `kCOLOR_DEFAULT`.

**3. En oscuro no se leía.** Dos capas distintas:

- El color del texto salía de `COLOR_BTNTEXT`, un color de sistema de 1995 que
  **no cambia con el modo oscuro** — la misma raíz que ya salió en `docs/18`.
  Negro sobre fondo oscuro.
- El fondo lo dibujaba el tema `TOOLBAR`, **que no tiene variante oscura**.
  `SetWindowTheme(DarkMode_Explorer)` no la encuentra y devuelve la clara, así
  que el resaltado del ratón salía casi **blanco** y se comía el texto claro.

  ```
  reposo:  fondo del panel (32,32,32)
  encima:  58,58,58
  pulsado: 72,72,72
  ```

  Son los tonos del propio Explorer: apenas por encima del fondo. Un botón de
  barra tiene que quedarse invisible hasta que se le apunta.

Rama `nap-045-boton-plano-con-texto` del SDK, `verify.ps1` en verde sobre build
limpio y **CI verde en las siete combinaciones**.

## La barra

Los quince botones pasan de `button_push()` a `button_flat()`. El ancho fijo de
los que **cambian de rótulo** (Grabar/Parar, Reproducir/Pausa, un fichero/aparte)
se muda de `button_width()` a `layout_hsize()`: `button_width` solo lo mira el
botón normal. Sin ancho fijo, la barra entera se recoloca al pulsarlos y los
botones vecinos se mueven bajo el ratón justo cuando vas a pulsarlos.

Y los dos que llevan icono necesitan `button_image_pos(..., ekGUI_POS_LEFT)`,
que es la forma de decir «dibuja los dos».

### Los separadores

Antes los grupos se marcaban solo con la separación en blanco, y funcionaba
**porque cada botón tenía su marco**: el ojo veía bloques. Con botones planos ya
no hay bloques que ver y la barra se lee como una fila larga de palabras
sueltas. Por eso ahora hay una raya fina entre grupos:

```
[fuente ▾] [Add track] │ [● Record] [▶ Play]   ...   │ [Folder…] [Default] │ [Small] [No commands] │ [ES]
Buffer: [2 min ▾]  Storage: [Lossless 16-bit ▾] │ [−] [+] │ [Freeze] [Live] [All]  ...  │ Export: [WAV ▾] │ [Separate files] [Save as…]
```

Dentro de un grupo, 4 px. A cada lado de una raya, 10.

Dos detalles que costaron una foto cada uno:

- La raya es un **View de 3 px con la línea dibujada dentro**, no un control de
  1 px. Ni un View ni una etiqueta bajan de unos 3 px de ancho, así que pedir
  uno de 1 daba un **bloque**. Dibujándola dentro, el ancho lo decide el dibujo
  y no el control.
- Va recogida 2 px por arriba y por abajo. Una raya que llega a los bordes
  parece un trozo de marco; recogida se lee como lo que es.

### `--theme` ya no manda en la barra

`--theme light` en un Windows oscuro dejaba la barra **a medias**: las etiquetas
en claro sobre un fondo que el sistema seguía pintando oscuro, y las rayas
blancas. Los tres colores de la barra (`chrome`, `chrome_tx`, `sep`) conviven
con controles **nativos**, y esos siguen al tema de Windows pase lo que pase.
Ahora salen de `gui_alt_color()` directamente. `--theme` sigue mandando en el
lienzo, que lo dibujamos nosotros entero.

Y de ahí salió otra ficha, **NAP-047**: desde que hay modo oscuro,
`layout_bgcolor()` **no hace nada** en Windows. El `WM_ERASEBKGND` de
`ospanel.c` devuelve «ya está borrado» en cuanto ve el modo oscuro, sin mirar si
el panel tiene un color pedido. El camino que lo pintaba queda más abajo y ya no
se llega.

## Cómo se fotografía un resaltado

El estado «ratón encima» lo decide Windows mirando **dónde está el cursor de
verdad**, así que no hay forma de fotografiarlo sin mover el ratón. `foto.ps1`
gana `-RatonBoton N`: enumera los controles de clase `Button` de la ventana, los
ordena por posición y apunta al centro del que se pida.

Por orden y no por coordenadas, a propósito: las coordenadas hay que sacarlas de
una foto anterior y **dejan de valer en cuanto la barra se recoloca**, que es lo
que pasa al cambiar el tamaño de la ventana. El orden de los botones, no.

Dos cosas más que hicieron falta y que no eran obvias:

- La ventana **tiene que estar delante**. `_osgui_hit_test` usa
  `WindowFromPoint`, que devuelve la de encima; con otra ventana tapando, el
  ratón está sobre esa y el botón no se entera. La primera foto salió sin
  resaltado por eso, y el Firefox del usuario era lo que tapaba.
- El cursor se devuelve a su sitio al terminar. Mover el ratón de alguien y
  dejárselo movido es de mala educación, y además falsea la foto siguiente.

## Estado

- `verify.ps1` del SDK sobre **build limpio**: 8/8 etapas, 0 avisos, 4/4 tests.
- CI del SDK: verde en las 7 combinaciones.
- SSB: Debug y Release compilan sin un aviso, `ctest` verde en las dos.
- Artefactos de audio: **0 interrupciones en 90 s**.
- Fotografiado con `PrintWindow`: la barra en reposo, la misma con el ratón
  sobre `Default` (un rectángulo gris apenas por encima del fondo, y nada más
  cambia), la chuleta de F1, y `--theme light` dando ya el mismo resultado.

Sin fotografiar: **el aspecto en un Windows en modo claro**. Esta máquina está
en oscuro y los controles nativos siguen al sistema, no a `--theme`, así que la
única forma honesta de verlo sería cambiarle el tema de Windows al usuario. La
demostración `GuiHello` del SDK sí ejerce ese camino y es el mismo código.
