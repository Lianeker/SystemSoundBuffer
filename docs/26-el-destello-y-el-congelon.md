# 26 — El destello al arrancar, y el congelón al exportar

Dos cosas que se ven al usar el programa. La primera costo tres mediciones y
dos hipotesis falsas; la segunda esta medida y sin arreglar.

## El destello blanco

Al abrir, durante medio segundo se ven el lienzo y los botones en blanco sobre
la ventana ya oscura.

### La primera respuesta fue falsa

Parecía la brocha de fondo de la clase de ventana del SDK, que además tenía un
defecto de verdad: en `WNDCLASSEX` un color de sistema se pasa como
`COLOR_X + 1`, y la rama de x64 se había dejado el `+ 1`, así que pasaba el
color 14, `COLOR_BTNHIGHLIGHT`, **blanco**. Se corrigió — es `NAP-048` — y el
destello **siguió igual**.

Lo que lo demostró: registrar las tres clases con una brocha **roja** y medir el
arranque leyendo el DC de la ventana con `BitBlt` cada 30 ms.

```
  581 ms   blanco 56,0 %   rojo 0,0 %
  919 ms   blanco 54,0 %   rojo 0,0 %
  997 ms   blanco  1,7 %   rojo 0,0 %
```

Cero rojo. No era la brocha.

Segundo experimento: teñir de rojo el relleno del botón plano y de verde el del
lienzo.

```
  542 ms   blanco 56,3   rojo 0,0   verde  0,0
  828 ms   blanco 54,0   rojo 0,0   verde  0,0
  906 ms   blanco  1,7   rojo 2,2   verde 49,9
```

Ni rojo ni verde durante el destello, y los dos de golpe al terminar. O sea que
los controles **no se pintaban de blanco: no se pintaban en absoluto**. Una
ventana hija que todavía no ha recibido su primer `WM_PAINT` se ve blanca.

### La causa

`ssbapp.c`, al arrancar:

```c
window_show(app->window);

app_apply_chrome(app);
app_reload_sources(app);     /* enumera dispositivos: abre COM y pregunta */
app_set_cmdmode(app, FALSE);
i_settings_load(app);
app_relabel(app);
```

La ventana se enseñaba y **después** se hacía el resto del arranque. Hasta que
eso terminaba, el bucle de mensajes no llegaba a repintar y los controles se
quedaban sin pintar a la vista.

### El arreglo, y lo que mide

`window_show` pasa al final. La ventana aparece algo más tarde, ya pintada.

```
antes:    54 % de blanco desde los 542 ms hasta los 906 ms
despues:  3,4 % en el primer fotograma, 1,7 % a partir del siguiente
```

Ese 1,7 % que queda es el texto claro sobre fondo oscuro, que el detector cuenta
como blanco: el estado normal de la ventana, no un destello.

La ventana aparece a los 636 ms en vez de a los 540. Es el cambio correcto:
antes salía pronto y rota, ahora sale un poco más tarde y entera.

### Un método que no servía

La primera medición se hizo con `PrintWindow` y dio «0,1 % de blanco desde el
primer fotograma», que era mentira: `PrintWindow` le pide a la ventana que se
dibuje, o sea que fuerza el repintado y borra justo lo que se quería medir. Hay
que leer el DC de la ventana con `BitBlt`, que copia lo que hay en pantalla sin
pedir nada.

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
