# 24 — La carrera que sólo Linux enseñó

Con la interfaz corriendo en Linux contra un servidor gráfico de verdad, murió
con SIGSEGV. No siempre: **una de cada diez ejecuciones**.

## Antes: cómo se llegó a poder verlo

La distribución de WSL estaba instalada con `--no-launch` y se ejecutaba como
root. Así el programa arrancaba y salía limpio, pero nunca llegaba a abrir
ventana: WSLg monta sus sockets para el usuario 1000, y root no los usa.

`wsl --install` sin `--no-launch` abre un diálogo interactivo pidiendo usuario y
contraseña, que no se puede contestar desde un guion. Lo equivalente es crearlo a
mano:

```sh
useradd -m -u 1000 -s /bin/bash ssb
printf '[user]\ndefault=ssb\n' > /etc/wsl.conf
wsl --terminate Ubuntu-24.04
```

Con eso `/mnt/wslg/runtime-dir/wayland-0` y `/mnt/wslg/.X11-unix/X0` aparecen y
pertenecen a ese usuario. Y ahí, en la primera ejecución con display, el fallo.

## La causa

`ssb_core/src/ssb_track.c`, en `ssb_track_create`:

```c
res = ssb_capture_open(src, i_on_audio, t, &t->channels, &t->rate, &t->cap);
...
t->peaks  = calloc(...);
t->acc    = calloc(SSB_BLOCK_FRAMES * t->channels, sizeof(int32_t));
t->encbuf = malloc(...);
```

La captura se abre **antes** de reservar el acumulador, el anillo y el mapa de
picos. Tiene que ser así: para dimensionarlos hay que saber cuántos canales y a
qué frecuencia, y quien lo dice es la propia captura.

Pero la captura arranca su hilo dentro de esa llamada y empieza a entregar. Si el
primer paquete llega antes de la reserva, `i_push_frames` escribe en `t->acc`,
que todavía es `NULL`:

```
Thread 11 "threaded-ml" received signal SIGSEGV
  i_push_frames (...) at ssb_track.c:243     243  dst[c] = s;
  i_on_audio    (...) at ssb_track.c:405
  i_stream_read (...) at linux/ssb_capture_linux.c:571
  ...  pa_mainloop_run
```

**El fallo no es de Linux.** Está en el código portable y lleva ahí desde
siempre. En Windows no salta porque WASAPI tarda bastante más en entregar el
primer paquete; el monitor de PulseAudio entrega en milisegundos y gana la
carrera una de cada diez veces.

## El arreglo

Una bandera `ready`, puesta a 1 al final de `ssb_track_create` bajo el mutex, y
comprobada al principio de `i_on_audio`. Hasta entonces el paquete se descarta.

Reservar antes de abrir la captura no es posible sin partir la apertura en dos
fases, y no merece la pena por un paquete de 20 ms que además llega cuando la
pista todavía no está grabando.

## Medido

```
antes:   9 arranques limpios de 10   (1 SIGSEGV)
despues: 25 de 25
```

`probes/linux-arranque.sh` es esa prueba: arranca y cierra la interfaz N veces y
cuenta los fallos. Existe porque **bajo gdb no se reproducía**: dos ejecuciones
seguidas salieron limpias y hubo que repetir dentro del propio gdb hasta cazarlo.
Un fallo que aparece una de cada diez veces no se ve ejecutando una vez.

Lo demás sigue igual: en Linux, `ctest` verde y la captura da 0 discontinuidades
con las dos fuentes; en Windows, `ctest` verde en Debug y Release, `huecos 0` y 0
interrupciones en 92 s.

## Lo que sigue sin verse

**La ventana.** Los sockets de WSLg están, el programa arranca y sale limpio,
pero ninguna ventana suya llega al escritorio de Windows: `msrdc` y `wslhost`
corren, y en la lista de ventanas de nivel superior no aparece ninguna de WSLg.
No es de SystemSoundBuffer, pero significa que **el aspecto de la interfaz en
GTK sigue sin comprobarse**.

Un aviso de método, de esa misma búsqueda: buscar la ventana por título se llevó
una ventana ajena, porque el título de una pestaña del navegador contenía el
nombre del proyecto. La captura se borró sin abrirla. Para identificar una
ventana valen el proceso y la clase; el título es de quien lo escriba.
