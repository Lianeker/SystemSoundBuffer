# 16 — La unión, y cuánto pesa un botón

## 1. Añadir una pista borraba el pasado

Dos síntomas, un defecto: al añadir una pista a mitad de la grabación no se
podía seleccionar nada anterior a ella, y reproducir respondía *«that selection
falls outside what every track covers»*.

`app_span` devolvía la **intersección** de lo que cubría cada pista. Una pista
recién creada cubre desde *ahora*, así que la intersección se colapsaba a
*ahora*.

La intersección tenía una razón real: que al exportar todas las pistas durasen
lo mismo, porque `ssb_track_save_wav` recortaba el tramo pedido al tramo de cada
pista (era el hallazgo H2 de `docs/01`). El arreglo va donde estaba el problema:
la exportación escribe el tramo **entero** y rellena con silencio lo que una
pista no cubra — que es exactamente lo que esa pista estaba grabando entonces.
El mecanismo de relleno ya existía desde `docs/12`; solo había que dejar de
recortar y aceptar que «este anillo no tiene nada aquí» no es un error.

Con eso, la interfaz puede ofrecer la **unión** sin desalinear nada.

```
seleccionable: 22.19 s (union de todas)
1  Headphones (...)   cubre 22.19 s de 120  huecos 0
2  powershell.exe     cubre 10.09 s de 120  huecos 0

...-1.wav  22.19 s  primer sonido t= 0.00 s
...-2.wav  22.19 s  primer sonido t=12.12 s
```

Misma duración las dos, y el audio de la pista tardía empieza en t=12.12 s:
donde se añadió. `tracks` dice ahora qué tramo cubre cada pista — el dato que
habría explicado el fallo de un vistazo.

## 2. ¿Pesan más los botones dibujados que los nativos?

La pregunta de fondo era si convenía sustituir toda la interfaz por controles
dibujados. Se midió con `probes/peso.cpp` (objetos USER y GDI del proceso) y con
el tiempo de pintado que la propia aplicación anota:

```
USER 59   GDI 59   handles 387   memoria 34.3 MB      (estable, 3 pistas)
dibujo del lienzo: 11.922 ms de media, 14.141 ms el peor
```

- **Recursos: la premisa era falsa.** La aplicación entera, con ~20 controles
  nativos, gasta **59 objetos USER**. El límite por proceso son **10 000**. No
  hay nada que ahorrar.
- **CPU: los dibujados salen peor, no mejor.** El lienzo cuesta ~12 ms por
  fotograma (Debug) y se repinta 25 veces por segundo porque la onda está viva.
  Un control nativo estático cuesta **cero** salvo cuando cambia. Meter la barra
  dentro del lienzo la convertiría en trabajo repetido 25 veces por segundo
  para siempre.

Aparte del peso hay razones que deciden solas: la lista de fuentes es un
desplegable, y rehacer a mano una lista emergente con teclado, desplazamiento y
cierre correcto es un proyecto entero. Y se perderían navegación con tabulador,
lectores de pantalla, alto contraste y métodos de entrada.

**Conclusión: no se sustituye la interfaz.** Los controles dibujados se quedan
donde de verdad hacen falta: dentro del lienzo, donde un control nativo no puede
entrar ni desplazarse con la lista.

### Los que sí son nuestros, mejor hechos

Los botones **M** y **X** de cada banda se han rehecho: esquinas redondeadas,
resalte al pasar el ratón y el texto **centrado midiéndolo** con
`draw_text_extents` en vez de con un desplazamiento fijo. Con un ancho a ojo la
letra se descentra en cuanto cambia la fuente del sistema o el escalado de la
pantalla, y se nota.

Son portables sin más: solo usan `draw2d`, que NAppGUI implementa en Win32,
Cocoa y GTK. De hecho se ven **igual** en los tres, en vez de heredar las
rarezas de cada uno.

## 3. Lo que se llevó NAppGUI: NAP-044

Como la respuesta a la pregunta del peso fue "no", el camino era el otro: llevar
lo que falta al SDK. Y lo que faltaba estaba claro — `Label` tiene
`label_color` y `label_bgcolor`, `osgui` ya exponía `osedit_color` y
`oscombo_color`, y `Button` no tenía nada. No era una carencia de diseño: era
una inconsistencia.

`button_color()` y `button_bgcolor()`, implementadas en los tres backends. Solo
tienen efecto en el botón **plano**, que es el único que dibuja NAppGUI; el
normal lo pinta el sistema y colorearlo obligaría a reimplementar su aspecto
nativo en tres sitios.

Detalle que condicionó el diseño: añadir parámetros a
`guictx_append_button_manager_imp` habría cambiado una firma pública, y la
política del proyecto lo prohíbe sin autorización. Va en su propia función de
registro, y los dos campos nuevos de `GuiCtx` van **al final** del struct para
no desplazar los que ya estaban. Feo, pero es la diferencia entre un cambio
aditivo y uno que rompe la ABI de un SDK ya instalado.

`verify.ps1` verde y **CI verde en las siete combinaciones**.

### El CI cazó lo que la máquina local no podía

El primer intento salió verde en `verify.ps1` y **rojo en macOS**, en las dos
arquitecturas: faltaba `#include <draw2d/color.h>` en `osbutton.m`. En Windows
me había pasado exactamente lo mismo y allí lo vio el compilador; en macOS no
había compilador que mirar.

Es la regla del proyecto («empuja y espera al CI») justificándose sola: aquí
solo se compila Windows, y `verify.ps1` puede salir verde con dos plataformas
rotas.

### Y una ficha más: NAP-045

Preparando la demostración visual salió otro defecto, anterior e independiente:
un botón plano con **solo texto y sin imagen** dispara `cassert(twidth > 0.f)` y,
sin aserciones, sale invisible. La causa está documentada en el propio código:
al botón plano se le pone la fuente diminuta a propósito para que el texto no le
fije el tamaño mínimo. Está pensado para iconos.

Costó dos intentos de demostración fallidos entender que el problema no era mi
código nuevo. Va en su propia ficha, como pide el proyecto, y NAP-044 usa el uso
soportado (con imagen).

## 4. Herramientas nuevas

- `probes/peso.cpp` — objetos USER/GDI, handles y memoria de un proceso, con
  vigilancia opcional para ver si algo crece.
- `probes/foto.ps1` — ejecuta un guion en la aplicación y fotografía la ventana
  con **PrintWindow**, que le pide a la ventana que se dibuje: otra aplicación
  encima ya no puede falsear la imagen.
- La orden `perf` en el modo comandos: milisegundos de pintado del lienzo,
  media y peor caso.

Las tres nacen de la misma idea: la pregunta «¿pesa más?» no se contesta
opinando.
