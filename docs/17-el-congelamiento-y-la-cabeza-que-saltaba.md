# 17 — El congelamiento, y la cabeza que saltaba

## 1. El programa se quedaba colgado al morir una entrada

El síntoma: una entrada que existía desaparece —unos auriculares Bluetooth que
se apagan— y el programa se queda congelado. Hubo que reiniciarlo.

La causa estaba en `i_update`, o sea **en el hilo de interfaz**:

```c
if (ctime - app->mute_clock >= 1.0)
    for (i = 0; i < app->ntracks; ++i)
        app->tracks[i].sys_muted = ssb_output_muted(&app->tracks[i].src);
```

`ssb_output_muted()` abre un enumerador COM, busca el endpoint y activa su
control de volumen. Una vez por segundo **y por pista**, en el hilo que pinta.
Con los dispositivos quietos no se nota nada. En cuanto uno está
desapareciendo, esas llamadas tardan segundos, y el hilo de interfaz no puede
hacer nada más entretanto: la ventana queda pintada y muerta.

Y de paso, la misma función hacía `CoInitializeEx` **sin su `CoUninitialize`**.
Cada llamada dejaba la cuenta de COM del hilo un punto más arriba, para siempre.
Es el mismo defecto que ya había aparecido en el codificador (`docs/10`) — dos
veces el mismo error, en dos sitios distintos.

### El arreglo, que además era lo otro que pedías

`ssb_core/src/ssb_watch.c`: un **vigilante** en su propio hilo que enumera
dispositivos y sesiones cada segundo y publica una **foto**. La interfaz solo
copia esa foto — memoria y un mutex — así que no puede bloquearse por mucho que
el sistema de audio se atasque.

Y como la foto se rehace sola, **el refresco dinámico de entradas y salidas sale
gratis**: lo que aparece o desaparece se ve sin pulsar nada. `version` solo sube
cuando cambia la lista, no cuando cambia el estado de mudo; si subiera siempre,
el desplegable se reconstruiría cada segundo y no se podría ni abrir.

Se añade también `ssb_watch_alive()`: una pista cuya fuente ya no está lo dice
en su banda. Antes se quedaba vacía sin explicar nada, que es lo peor de los dos
mundos.

### Medido

`probes/run-fuente-muere.ps1`: se graba con dos pistas, se **mata** el proceso
que es una de las fuentes y se mira si el programa sigue vivo.

```
> tracks                                    (antes de matar)
1  Headphones (...)   cubre  8.02 s  huecos 0
2  powershell.exe     cubre  8.02 s  huecos 0

  ... se mata el proceso ...

> tracks                                    (10 s despues)
1  Headphones (...)   cubre 19.20 s  huecos 0
2  powershell.exe     cubre 19.16 s  huecos 1
> list
  (el powershell muerto ya NO aparece; si aparece el pid nuevo)
> stop
```

El guion siguió avanzando tras la muerte de la fuente: el programa no se colgó.
La lista se refrescó sola. Y la pista afectada anotó `huecos 1` — el corte real,
detectado y contabilizado en vez de pasar desapercibido.

## 2. La cabeza de reproducción saltaba al hacer clic

La cabeza se dibujaba respecto a la **selección viva**:

```c
ssb_time a = (app->sel_a < app->sel_b) ? app->sel_a : app->sel_b;
ssb_time at = a + play_position;
```

Y un clic empieza una selección nueva: `sel_a` cambia, y la cabeza se teletransporta
a un sitio que no tiene nada que ver con lo que está sonando. Peor: `i_OnUp`
borraba la selección si no había arrastre, con lo que la cabeza desaparecía.

Dos cambios:

- La cabeza se ancla a **dónde empezó la reproducción** (`app->play_from`), no a
  la selección. Lo que suena no cambia porque el usuario haga clic en otro sitio,
  así que la cabeza tampoco debe.
- Al parar, la cabeza desaparece y la referencia vuelve a la selección, que es lo
  que pediste.

Y ya que el gesto quedaba libre, se aprovecha la distinción que propusiste:
**arrastrar marca el tramo, clic seco salta la escucha**. Un clic sin arrastre
mientras suena algo llama a `ssb_play_seek()` y **devuelve la selección como
estaba** — porque borrarla era justo la mitad del problema.

El salto se aplica en el hilo de reproducción, no en el de interfaz: mover el
fichero desde fuera mientras el otro hilo lo está leyendo es pedir una carrera.
La interfaz solo deja anotado a dónde quiere ir.

## 3. La barra, más ordenada

Lo que más desordenaba era que **Grabar y Reproducir estaban en filas
distintas** siendo las dos acciones principales. Ahora van juntas y primeras.

```
[fuente ▾] [Add track]   [● Record] [▶ Play]        [Folder…] [Default]  [Small] [No commands]  [ES]
Buffer: [5 min ▾]  Storage: [Lossless 16-bit ▾]  [−][+]  [Freeze][Live]  [All]     Export: [WAV ▾]  [Separate files] [Save as…]
```

Los grupos se marcan **con la separación**, no con adornos: 4 px dentro de un
grupo, 18 entre grupos. Hace el trabajo que en otros programas hacen las líneas
divisorias, sin añadir un solo píxel de tinta.

Dos detalles que costaron una captura cada uno:

- El espaciador que empuja el resto a la derecha tiene que ser una **etiqueta
  vacía**, no un botón. Puse el ensanchado sobre la columna de `Select all` y el
  botón se estiró él, dejando a los demás apretados.
- Los dos botones que **cambian de rótulo** (Grabar/Parar, Reproducir/Pausa)
  llevan ancho fijo. Sin eso, la barra entera se recoloca al pulsarlos y los
  botones vecinos se mueven bajo el ratón justo cuando vas a pulsarlos.

Los rótulos largos se acortaron donde no se pierde nada: «Freeze view» → «Freeze»,
«Go live» → «Live», «Select all» → «All». La ventana útil son ~1024 px lógicos
(la pantalla escala al 125 %), y no caben de otra forma.

## 4. Los botones de banda, rectangulares

Se los redondeé y pediste rectangulares. Tienes razón por una razón concreta: un
botón de 22×16 con esquinas redondeadas gasta la mitad de su borde en curvas y
se lee peor. A ese tamaño lo simple gana.

Sobre sacarlos de la pista: **no pueden ser nativos**. Un control del sistema no
se puede meter dentro de un `View` ni desplazarse con la lista al hacer scroll, y
estos tienen que moverse con su banda porque pertenecen a ella. Que los nativos
sean más ligeros (`docs/16`) no cambia eso: no es una cuestión de peso sino de
dónde pueden vivir.

Lo que sí se les añadió es respuesta al ratón: se resaltan al pasar por encima.
Un botón dibujado que no reacciona no parece pulsable, y ahí es donde los
propios se notan peor que los del sistema.

## 5. Estado

Debug y RelWithDebInfo compilan sin un aviso, `ctest` pasa en las dos, y los
artefactos siguen a cero: **0 interrupciones en 40 s** de audio en dos fuentes
después de todos estos cambios.
