# 18 — El tema oscuro que nunca existió

## La causa raíz no era la que yo pensaba

El síntoma: la barra de la aplicación salía con **botones blancos sobre fondo
oscuro**. Y tenía sentido pensar que faltaba pedirle a Windows el modo oscuro
para los controles nativos, que es lo que decía `NAP-042` en el backlog.

Hacía falta, sí. Pero la raíz estaba en otra parte, y sin arreglarla primero lo
demás no habría servido de nada. `nappgui_src/src/osgui/win/osglobals.c:42`:

```c
case ekSYSCOLOR_DARKMODE:
{
    uint32_t c = GetSysColor(COLOR_3DFACE);
    real32_t r = ..., g = ..., b = ...;
    return (.21 * r + .72 * g + .07 * b) < .5 ? TRUE : FALSE;
}
```

Deduce el tema del **brillo de `COLOR_3DFACE`**. Pero los colores de sistema de
Win32 son de la época de Windows 95 y **no cambian** cuando el escritorio se
pone en oscuro: `COLOR_3DFACE` sigue siendo gris claro. Así que esto devolvía
**siempre FALSE**.

Es decir: `gui_dark_mode()` mentía en Windows, y con él todos los
`gui_alt_color()` de todas las aplicaciones. SystemSoundBuffer se veía oscuro
solo porque yo le pasaba `--theme dark` a mano; sin esa bandera, en un Windows
en modo oscuro, se veía claro.

El ajuste vive en el registro, en `AppsUseLightTheme`, y es lo único que sabe la
verdad.

## Lo que además hizo falta

Con la detección arreglada, `SetWindowTheme(DarkMode_*)` sigue sin bastar: pone
en oscuro el marco, las barras de desplazamiento y los estados del ratón, pero
el **relleno** de los controles sale de los colores de sistema. Por eso hubo
que, además:

- **Pintar el fondo nosotros** en `WM_ERASEBKGND`. La clase de ventana se
  registra con `COLOR_BTNFACE + 1`, que es un índice de color de sistema.
- **Atender `WM_CTLCOLOR*`** (botones, listas, estáticos, campos de texto).
- **Dar color a lo que dibuja el propio SDK**: las listas y tablas van por
  `osdrawctrl.cpp`, que rellenaba con `COLOR_WINDOW` y sacaba el texto del tema
  de `LISTVIEW` — que devuelve el color claro y dejaba el texto ilegible.
- **Recorrer la descendencia de cada control**: el desplegable de NAppGUI es un
  `ComboBoxEx32` que crea *dentro* su propio `COMBOBOX` y su `EDIT`. Esos hijos
  no pasan por la creación de controles del SDK, así que se quedaban blancos
  dentro de un control ya oscuro.
- **La barra de título**, por `DwmSetWindowAttribute` — esto sí es API
  documentada.

Y un detalle de orden que costó una aserción: el tema se aplica **antes** de
instalar el WndProc de NAppGUI. `SetWindowTheme` manda `WM_THEMECHANGED` en el
acto, y ese mensaje llegaba a un control a medio construir, con su `type` aún
sin asignar.

Los dos ordinales de `uxtheme.dll` (133 y 135) se resuelven en tiempo de
ejecución y con camino de degradación: si no están, no se hace nada y la
aplicación se ve en claro. Un SDK no puede depender de que un ordinal sin
documentar siga ahí.

**`verify.ps1` verde y CI verde en las siete combinaciones.** Está en la rama
`nap-042-modo-oscuro-win32` del repo del SDK.

### Los botones cuadrados salieron gratis

La ficha anotaba como extra «un estilo de botón cuadrado y minimalista». No hizo
falta: con el tema oscuro aplicado, los botones nativos de Windows 11 se pintan
planos, sin degradado y con borde de un píxel. Justo lo que se pedía, sin
inventar un control propio.

## Comandos de teclado

«Comandos» eran atajos de teclado, no la consola escrita — que se queda, porque
sirve para otra cosa.

```
Ctrl+R  grabar o parar          Ctrl+E  congelar la vista
Ctrl+L  reproducir o pausar     Ctrl+G  volver al directo
Ctrl+H  parar la reproduccion   Ctrl+M  modo reducido
Ctrl+A  seleccionar todo        Ctrl+K  consola de comandos escritos
Ctrl+S  guardar la seleccion    F1      mostrar u ocultar esta lista
Ctrl+J  un fichero / aparte
Ctrl+D  carpeta de destino
```

Todos con `Ctrl` a propósito: la consola es un campo de texto, y un atajo sin
modificador se comería lo que se está escribiendo.

Y hay una **chuleta** (F1, o la orden `keys`) dibujada sobre la onda. Unos
atajos que no se pueden consultar no existen: o se memorizan el primer día o no
se usan nunca. Va encima del lienzo y no en un diálogo aparte para poder mirarla
mientras se trabaja, que es cuando hace falta.

La orden `keys` existe además del atajo por una razón práctica: **un atajo que
solo se puede probar pulsándolo no se puede probar automáticamente**. Con la
orden, la comprobación entra en un guion.

## Dos fallos propios cazados por mirar la captura

1. **«THIS SOURCE IS GONE» en falso.** El vigilante de dispositivos tarda su
   primer ciclo en publicar la foto, y durante ese rato la lista está vacía;
   `ssb_watch_alive()` interpretaba «lista vacía» como «ha desaparecido» y toda
   pista nacía marcada como perdida. Vacío significa *todavía no se sabe*.
   Además, el dispositivo **por omisión** se guarda con el id vacío: no hay nada
   que comparar y siempre existe uno, así que ese caso se da por vivo.

2. **El campo de comandos seguía blanco.** Un `Edit` sin color propio no pasaba
   por ninguna de las ramas que había tocado. Es donde más canta, porque suele
   ser lo más ancho de la barra.

Ninguno de los dos aparecía en el registro de la aplicación ni en ningún test.
Se vieron mirando la captura, que para lo visual sigue siendo el único
instrumento honesto.

## Estado

Debug y RelWithDebInfo sin un aviso, `ctest` verde en las dos, y los artefactos
de audio siguen a cero: **0 interrupciones en 36 s** después de todos estos
cambios.
