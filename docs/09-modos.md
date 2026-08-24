# La vista se apega al buffer, y dos modos nuevos

Fecha: 2026-08-24. Continua [`08-corrupcion-y-vista.md`](08-corrupcion-y-vista.md).

## La directiva: el buffer no depende de lo visual

Regla del proyecto, dicha explicitamente por el usuario: **el buffer nunca
depende de lo visual; lo visual se apega al buffer.**

Auditado. El motor cumple por construccion: `ssb_core` no tiene una sola
dependencia de GUI (decision D1) y nada en el consulta a la interfaz. Pero la
interfaz **guardaba su propia copia** de la duracion del buffer
(`app->buffer_secs`) y dibujaba a partir de ella. Mientras coincidieran, no se
notaba; en cuanto dejaran de coincidir —por ejemplo con pistas creadas con
tamanos distintos— la vista habria estado mintiendo.

Corregido: se anadio `ssb_track_buffer_seconds()` al motor y la vista **pregunta**
(`app_buffer_secs()` recorre las pistas y toma la mayor). `app->buffer_secs`
queda solo como valor por omision para pistas nuevas. Si algun dia una pista
cambia de tamano por su cuenta, la vista se entera sola.

No era un fallo visible todavia. Era una copia de estado esperando a divergir, y
la directiva es precisamente para que eso no llegue a pasar.

## Modo reducido (Ctrl+M)

Lo minimo para grabar: se oculta la segunda fila de la barra, las bandas bajan a
46 px, desaparece la regla de tiempo y la cabecera de cada pista se queda con el
nombre de la fuente. Siguen estando los botones M y X, el nivel y el indicador
rojo de grabacion.

Se activa con el boton **Small**, con **Ctrl+M**, o al arrancar con
`--mode small`.

## Modo comandos (Ctrl+K)

Una linea de entrada y una consola de siete lineas dibujada en el lienzo. La
aplicacion entera desde el teclado:

```
rec | stop | add <fuente> | close <n> | mute <n> | solo <n>
buffer <seg> | zoom <seg> | sel <seg> | all | save [seg]
export wav|mp3|m4a | folder <ruta>|default | list | tracks
fuente: output | output:<n> | input | app:<nombre> | app:<pid>
```

**Cada comando llama exactamente a la misma funcion que su boton.** No hay un
camino "de comandos" paralelo: `app_set_recording`, `app_add_track`,
`app_select_last`, `app_quick_save`... son compartidos. Un camino que se
duplica es un camino que diverge.

Verificado sin tocar el raton: arrancar sin pistas, `add output`,
`add app:<pid>`, `buffer 20`, `rec`, esperar, `tracks`, `stop`, `save 6`.
Resultado: dos WAV de 1 151 992 y 1 152 036 bytes = **6.00 s exactos**, que es lo
que pedia `save 6`. Y la consola en pantalla:

```
grabando
> tracks
1 Speakers (Realtek(R) Audio)  48000 Hz  0.7 MB  x3.0  buffer 20 s
2 powershell.exe  48000 Hz  0.9 MB  x2.4  buffer 20 s
> stop
parado
> save 6
```

Notese que `tracks` muestra `buffer 20 s` preguntandoselo al motor, no a una
variable de la interfaz.

## Dos trampas del camino

**`edit_OnChange` no salta con Enter.** En NAppGUI solo se dispara al perder el
foco (`_osedit_resign_focus`, `osgui/win/osedit.c:459`). Con Enter, el texto se
quedaba en el control y los comandos se iban concatenando en la linea sin
ejecutarse nunca. Solucion: registrar **Return como atajo de ventana** y leer el
texto con `edit_get_text()`.

**`small` es un macro de Windows.** `rpcndr.h` lo define como `char`, asi que
`int small` se convierte en `int char` y el fichero deja de compilar — pero solo
en las unidades que incluyen `Windows.h`, lo que hace el error especialmente
confuso. El campo se llama `compact`.

## Consola visible sin pistas

`i_OnDraw` salia antes de tiempo cuando no habia pistas, asi que en modo
comandos no se veia la respuesta a `help` ni a `list` — justo los dos comandos
que se usan **antes** de tener pistas. Ahora la consola se dibuja igual.
