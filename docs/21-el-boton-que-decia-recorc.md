# 21 — El botón que decía «Recorc»

Salió al hacer la captura de portada para el repositorio. Es la ventaja de
fotografiar: obliga a mirar.

![la barra, con el botón cortado](img/recorc.png)

`Record` sin la `d`. Y con el idioma en español habría sido peor: «Reproducir»
es casi el doble de largo que «Play».

## Por qué

Los dos botones que **cambian de rótulo** —Grabar/Parar, Reproducir/Pausa—
llevan ancho fijo. Sin él, la barra entera se recoloca al pulsarlos y los
botones de al lado se mueven bajo el ratón justo cuando vas a pulsarlos. El
problema no era ese; era el número:

```c
button_width(app->btn_input, 96);   /* medido a ojo, mirando "Record" */
```

Puesto a ojo y en inglés. Con `Reproducir` no cabía.

Al pasar los botones a planos (`docs/19`) el ancho se mudó a `layout_hsize`, y
ahí apareció el segundo detalle: **la holgura por omisión de un botón plano con
icono es medio icono**. El icono son 18 px, así que la holgura son 9 — y se
calcula **sin mirar el texto**. Un icono pequeño con un rótulo largo va siempre
justo, y aquí se quedaba corto por unos pocos píxeles.

## Cómo se midió, que es lo que importa

La primera hipótesis fue buena y falsa: que `font_extents` midiera en unidades
lógicas y el dibujo en píxeles de pantalla, con el 125 % de escala de por medio.
Cuadraba con el tamaño del recorte. Pero al ir a leer `osfont_extents` resulta
que mide con **el mismo HFONT** con el que luego se dibuja, así que no hay tal
desajuste.

En vez de seguir razonando, se midió. Subiendo la holgura y fotografiando:

| holgura | resultado |
|---|---|
| 9 (la de omisión) | `Recorc` |
| 12 | entero |
| 16 | entero |
| 40 | entero, y con demasiado aire |

Doce ya bastaba: el desbordamiento eran unos pocos píxeles, no el 25 % que
predecía la hipótesis del DPI. Se deja en **20**: margen de sobra sobre el
umbral medido, y el aire que tiene un botón de barra de verdad.

Con eso el ancho fijo se puede **calcular** en vez de ponerlo a ojo — se mide el
rótulo más largo de los que ese botón va a llevar, no el que lleva ahora:

```c
ssb_txt play[3];
play[0] = TXT_PLAY;  play[1] = TXT_PAUSE;  play[2] = TXT_RESUME_PLAY;
layout_hsize(r0, 4, i_btn_width(app, play, 3, app->ico_play));
```

Comprobado en los dos idiomas: `Record`/`Stop` y `Grabar`/`Parar`/`Reproducir`
entran enteros.

## Y la instrumentación, otra vez

Al pasar la regresión de artefactos después de este cambio salió **1
interrupción en 91 s**. Cero desde hacía semanas, y de repente una.

No era una regresión. El detector marcaba tramos de silencio exacto de **1
frame** —0.02 ms— y ese no es un empalme: es la onda cruzando el eje. En un
pasaje tranquilo los dos canales cruzan en la misma muestra y dan un cero
perfecto. El motor decía `huecos 0` en las dos pistas y tenía razón.

Es la tercera vez que el instrumento miente antes que el código (`docs/12`,
`docs/20`), y la causa es siempre la misma: la medida se escribía a mano cada
vez. Así que ahora vive en `probes/artefactos.py`, con los umbrales
justificados en el propio fichero, y `run-artefactos.ps1` la ejecuta sola:

```
=== interrupciones ===
interrupciones: 0 en 91 s
```

**16 frames** de mínimo, que son 0.33 ms: mil veces más corto que el hueco real
más pequeño que hemos medido (12.7 ms) y a la vez imposible de confundir con un
cruce por cero.

## Estado

Debug y Release sin un aviso, `ctest` verde en las dos, `selftest` con todas sus
comprobaciones —incluida la sección de mezcla— y **0 interrupciones en 91 s**
con el detector arreglado.
