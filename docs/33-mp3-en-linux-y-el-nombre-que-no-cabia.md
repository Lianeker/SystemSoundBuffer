# 33 — MP3 en Linux, y el nombre que no cabía

Dos cosas que salieron de usar el programa en un Kubuntu de verdad: el
desplegable de fuentes pintado encima de los botones, y la exportación a MP3
cayendo a WAV.

## El desplegable que se comía la barra

`layout_hsize(r0, 0, 240)` fija el ancho **mínimo** del desplegable de fuentes.
Pero un desplegable pide como ancho natural el de su entrada más larga, y ese
gana. Con un dispositivo llamado `Family 17h/19h/1ah HD Audio Controller
Speaker` la caja crecía hasta unos 440 px y «Add track», «Record» y «Play»
quedaban pintados por debajo.

Reproducido con un sink virtual de nombre largo, que es lo que hizo falta para
verlo aquí: en el banco de pruebas el dispositivo se llama `RDP Sink` y nunca
pasaba.

El arreglo acorta el nombre, y hay dos decisiones dentro:

- **Por el medio, no por el final.** Lo que distingue dos dispositivos suele
  estar en los dos extremos: `...Controller Speaker` y `...Controller
  Microphone` solo se diferencian al final.
- **Midiendo, no contando.** El primer intento cortaba a 44 caracteres y seguía
  desbordándose; el ancho depende de la fuente, del tamaño del sistema y de qué
  letras sean. Ahora se mide con `font_extents` y se baja hasta que entra.

Y se acorta **solo el nombre**, no la línea entera: si el presupuesto lo consume
el prefijo `Output 0  -  `, del dispositivo no queda casi nada.

No es cosa de Linux, aunque allí se viera: cualquier nombre largo lo provoca.

## MP3 con LAME, cargada en tiempo de ejecución

En Windows lo hace Media Foundation, que viene con el sistema. En Linux no hay
codificador de MP3 en la plataforma, así que `ssb_encode` devolvía
`ssb_err_platform` y la interfaz se quedaba con el WAV. Eso funcionaba y lo
decía, pero no es lo que se prometía en la tabla del README.

Ahora usa **libmp3lame cargada con `dlopen`**. No es un rodeo, es lo que
mantiene tres propiedades a la vez:

- No hace falta `libmp3lame-dev` para compilar.
- **El binario arranca sin ella.** Enlazarla haría que el programa no abriese en
  un sistema sin MP3, que es mucho peor que no tener MP3. Comprobado:

```
$ objdump -p ssbgui | grep NEEDED
  libpulse-simple  libpulse  libgtk-3  libgdk-3  libpangocairo  libpango
  libcairo  libgdk_pixbuf  libgio  libgobject  libglib  libm  libc
```

  Trece, y ninguna es LAME. Aparece en `ldd` solo porque libsndfile la arrastra.
- LAME es LGPL, y cargarla así no traslada sus obligaciones al binario.

AAC no se implementa. No hay codificador libre equivalente sin fricción de
licencias, y prometer AAC para dar WAV sería peor que decir que no está.

De paso hizo falta un lector de WAV, que no existía —solo había escritor—. Lee
los trozos RIFF en vez de dar por hecho que `data` está en el desplazamiento 44,
porque `ssb encode` también acepta ficheros de fuera.

### Lo que mide

`probes/linux-mp3.sh` codifica un tono conocido, lo **decodifica de vuelta** con
mpg123 y comprueba que sigue ahí. Sin el viaje de vuelta esto solo diría que
sale un fichero, y un fichero de silencio también sale.

```
  estereo-16    wav   768044 B  ->  mp3   97344 B   dominio 3242395x
  estereo-24    wav  1152044 B  ->  mp3   97344 B   dominio 3280755x
  mono-16       wav   384044 B  ->  mp3   97344 B   dominio 3364657x
  m4a: no se genera, como debe ser
```

Y desde la interfaz, que es el camino que usa la gente:

```
> export mp3
[msg] Saved 1 of 1 tracks, 5.00 s each: .../2026-08-29_20-17-36-N.mp3
    dominio 2725995x   OK: el tono de 480 Hz esta en la grabacion
```

## Una prueba le dejaba el estado puesto a la siguiente

Al encadenar las sondas, la de la interfaz empezó a fallar. La causa no estaba
en ella: la prueba del MP3 había hecho `export mp3`, SSB **guarda los ajustes**,
y la siguiente buscaba un `.wav` que ya no se escribía.

O sea que las sondas leían y escribían el fichero de ajustes real de quien las
ejecuta. Ahora cada una se lleva su `XDG_CONFIG_HOME` dentro del directorio de
trabajo. No es solo higiene entre pruebas: también dejaba tocada la
configuración del usuario.

## Lo que sigue abierto

El tema oscuro en KDE. Medido sobre una captura del usuario, el programa se
dibuja **entero en claro** (lienzo 246,247,249 y barra 240,240,240) mientras GTK
pinta sus controles oscuros, o sea que `gui_alt_color` resolvió en claro. No
reproduce aquí: ni bajo Xvfb ni bajo WSLg, ni por Wayland ni por X11, con
`GTK_THEME=Adwaita:dark` sale correcto. Falta saber qué devuelve la detección en
un Kubuntu con el puente de GTK de KDE. `probes/linux-tema.sh` es la red para
cuando se sepa.

Como apaño mientras tanto, `GTK_THEME=Adwaita:dark ./ssbgui` fuerza el camino
que sí funciona.
