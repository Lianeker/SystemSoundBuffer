# 26 — El destello al arrancar, y el congelón al exportar

Dos cosas que se ven al usar el programa. Una está arreglada; la otra, medida y
no.

## El destello blanco

Al abrir, durante un instante se ve el lienzo y los botones en blanco sobre la
ventana ya oscura. Son dos defectos del SDK, y el segundo llevaba ahí desde
siempre.

`nappgui_src/src/osgui/win/osgui_win.cpp`, al registrar la clase de la ventana:

```c
#if defined(__x64__)
    wc.hbrBackground = (HBRUSH)(uint64_t)(COLOR_BTNFACE);
#else
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
#endif
```

En `WNDCLASSEX` un color de sistema se pasa como **`COLOR_X + 1`**, porque el 0
significa «sin brocha». La rama de x86 lo hace bien; la de x64 se dejó el `+ 1`
al añadir el cast que silencia el aviso C4306. Con `COLOR_BTNFACE` = 15, pasar
15 significa color **14**, que es `COLOR_BTNHIGHLIGHT`: **blanco**. O sea que en
x64 el fondo de toda ventana era blanco en vez del gris de botón.

Y aunque fuera gris, en oscuro también destellaría. Así que las tres clases
propias del SDK se registran ahora con la brocha oscura cuando el sistema lo
está. `_osdark_start()` ya corre antes de los registros, así que ahí se sabe.

Lo que Windows pinta antes del primer `WM_PAINT` pasa a ser del color del fondo
definitivo, y el destello deja de verse aunque siga existiendo.

Comprobado leyendo `GCLP_HBRBACKGROUND` de la ventana: devuelve un objeto GDI de
verdad y no un índice pequeño de color de sistema, que es lo que devolvía antes.

**No se puede fotografiar.** `PrintWindow` le pide a la ventana que se dibuje, o
sea que fuerza el repintado y el destello desaparece. La evidencia es la captura
de pantalla del usuario y la lectura del código. Es `NAP-048` en el backlog del
SDK.

## El congelón al exportar: medido, no arreglado

Al dar a Reproducir o a Guardar con un tramo largo, la ventana se queda muerta
unos segundos. Medido con `probes/run-export-bloquea.ps1`, guardando 140 s de
dos pistas:

```
antes de guardar:  dibujo del lienzo 17,0 ms de media, 3283 fotogramas
durante:           dibujo del lienzo 2505,2 ms de media, 2 fotogramas
huecos de captura: 0 -> 2 y 0 -> 3
```

No es solo la ventana: **se pierde audio**. El motor anota los huecos.

La causa es una sola línea de `ssb_track_save_wav`: el mutex de la pista se
mantiene cogido durante toda la escritura del WAV. Mientras dura, ni la captura
puede entregar ni el dibujo puede leer los picos.

### Lo que se hizo

La exportación —y reproducir, que usa el mismo camino— se mudó a un hilo propio.
El hilo no toca la interfaz: escribe ficheros y deja el resultado en un `SsbJob`,
y `i_update` lo recoge ya en el hilo de interfaz para mostrar el mensaje y abrir
la reproducción. Mientras trabaja, la barra dice «Preparando el audio...» y un
segundo intento se rechaza en vez de solaparse.

Eso saca el trabajo del hilo de interfaz, pero **no arregla el congelón**: el
bloqueo no era el trabajo, era el mutex. La cifra sigue en 2505 ms.

### Lo que se intentó y no valió

Soltar el mutex durante la escritura y aplazar solo el descarte del anillo
(`ssb_ring_hold`), con la idea de que a un lector que ya sabe cuántos bloques va
a recorrer no le molesta que se añada por la cola.

El resultado fue bueno en las dos cifras que se buscaban:

```
dibujo del lienzo: 22,0 ms de media    huecos: 0 y 0
```

y malo en la que importa: **el fichero exportado salió a la mitad**. 70,49 s de
los 135,17 pedidos, en la pista que seguía grabando. El lector necesita más
garantías que «no me muevas el principio»; con la captura rotando segmentos y
escribiendo con stdio a la vez, lo que hay en el fichero y lo que el índice dice
que hay no coinciden.

Revertido. El congelón sigue, y la exportación es correcta, que es el orden
correcto de prioridades para una grabadora.

Queda apuntado en el propio `ssb_track.c` y hace falta antes de intentarlo otra
vez: **una prueba que exporte mientras se graba y compare la duración pedida con
la escrita**. Sin eso, el fallo se cuela — de hecho se coló, y solo apareció al
mirar el tamaño de los ficheros.

## Dos cosas más del buffer

- **`buffer N` no llegaba a las pistas nuevas.** Un valor a medida —los que solo
  se alcanzan por orden escrita— no tiene preajuste que marcar, así que el
  desplegable se quedaba donde estaba; y la pista nueva leía el desplegable. Se
  veía en el registro: `buffer 20`, y la pista siguiente naciendo con 120. Ahora
  se lee `buffer_secs`, que es el valor de verdad.
- **`--buffer N` movía el desplegable pero no `buffer_secs`.** La primera pista
  nacía con el valor guardado de la sesión anterior. Los dos van juntos ahora.

## Estado

`ctest` verde en Debug y Release, `huecos 0`, 0 interrupciones en 92 s. El SDK,
`verify.ps1` en verde con 0 avisos.
