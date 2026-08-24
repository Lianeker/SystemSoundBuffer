# 11 — Los chasquidos, y unos ajustes que no se quedaban

Dos quejas sin relación entre sí: «la carpeta seleccionada no se está quedando
grabada, tengo que seleccionarla cada vez» y «en todos los audios que grabo hay
unos ruidos artefactos extraños, ya sea wav o mp3».

## Los chasquidos

Lo primero fue mirar los ficheros. Estaban en la carpeta de música del usuario,
así que no hubo que reproducir nada: el defecto ya estaba grabado.

La firma resultó inconfundible. En medio de una onda perfectamente continua:

```
    4557  L=   2886  R=   4724
    4558  L=   2889  R=   4687
    4559  L=   2887  R=   4646
    4560  L=      0  R=      0     <-- cuatro frames de cero
    4563  L=      0  R=      0
    4564  L=   2887  R=   4605
    4565  L=   2882  R=   4559
```

No falta audio: la onda continúa exactamente donde la dejó (2887 → 2887). Lo que
hay es **silencio insertado**. Diez rachas de este tipo en 1.06 s, de 1 a 4
frames cada una — 0.4 ms de silencio en total, y aun así diez discontinuidades
duras por segundo. Eso es lo que se oye.

Y estaban espaciadas 4096 o 8192 frames: los límites de bloque.

La causa está en `ssb_core/src/ssb_track.c`, en `i_on_block`, que monta el WAV a
partir de los bloques del anillo:

```c
    /* Hueco por delante del bloque: el tiempo paso pero no llegaron frames. */
    if (bt > s->next)
        res = i_pad_until(s, cap);
```

Las marcas de tiempo (`bt`) vienen del QPC que WASAPI adjunta a cada paquete. Ese
reloj no cae exactamente sobre los límites de frame: entre dos bloques seguidos
la marca baila unos pocos frames en un sentido o en el otro. Es ruido de medida.
Las muestras que la tarjeta entregó **son** contiguas.

Rellenar ese baile con ceros es lo que metía los chasquidos.

Aquí hay una tensión real, no un descuido tonto. Entre pistas distintas, la marca
de tiempo es lo único que las alinea — es la lección de `docs/03` y no se toca.
Dentro de una misma pista, en cambio, los frames llegaron pegados y la marca solo
aporta su propio error de medida. La corrección es una **holgura de empalme**:

```c
#define SSB_SPLICE_SLACK_MS 5
```

Por debajo de 5 ms el bloque se empalma pegado al anterior y la marca se ignora.
Por encima, el hueco es real (una transición de flujo de la fuente ronda los 12
ms, medido en `docs/03`) y se tapa con silencio, que es lo que mantiene las
pistas alineadas. Entre el jitter (unos pocos frames) y el hueco real más pequeño
que hemos medido hay tres órdenes de magnitud, así que el umbral no es delicado.

Un detalle que sí importa: al empalmar, el cursor se re-ancla a la marca del
bloque **siguiente**, no a una cuenta de frames acumulada. Así el error se queda
en unos pocos frames para siempre en vez de crecer sin techo a lo largo de una
grabación de horas.

### Lo que costó medirlo

El primer detector contaba rachas de ceros. Dio 3208 en 21 s — absurdo. Estaba
contando los silencios digitales reales entre repeticiones del sonido de prueba.

El segundo exigía valores grandes a ambos lados. Dio 7, y al mirarlos uno por uno:

```
    25452  L=   -959
    25453  L=      0     <-- "artefacto"
    25454  L=    997
```

Un cruce por cero que cayó justo en 0. Natural, no un defecto.

La firma correcta es que la onda se interrumpe **sin cambiar de signo**: de 2887
a 0 y de vuelta a 2887. Con ese criterio, y aplicado igual a los dos ficheros:

| | interrupciones |
|---|---|
| antes (fichero del usuario, 1.06 s) | 5 → **4.70/s** |
| después (43 s de audio, dos pistas) | 0 → **0.00/s** |

Confirmación adicional del diagnóstico: los `mod 4096` de los artefactos de antes
eran 464, 466, 469, 470 — crecen exactamente por los frames que se iban
insertando en cada empalme.

## Los ajustes

Aquí había dos fallos distintos, y ninguno de los dos era el que parecía.

**No había persistencia ninguna.** `i_create` reponía `app->savedir` en cada
arranque. Ahora hay un fichero de ajustes en
`%APPDATA%\SystemSoundBuffer\ssb.cfg` (`~/.config/SystemSoundBuffer/` fuera de
Windows), texto plano, una `clave=valor` por línea. Guarda carpeta, idioma, tema,
buffer, compresión y formato de salida.

Va al directorio del usuario y no junto al ejecutable a propósito: si dependiera
del directorio de trabajo se perdería en cuanto la aplicación se lanzase desde
otro sitio, que es justo lo que hace un acceso directo — y ya sabemos de
`docs/10` lo movedizo que es ese directorio.

Lo que **no** se guarda es igual de deliberado: las pistas y el estado de
grabación no se restauran. Que un programa que graba audio arranque solo
capturando lo que oye sería una sorpresa desagradable.

**Y la carpeta del propio diálogo se perdía.** Si el usuario navegaba a otra
carpeta dentro del diálogo de guardar en vez de usar el botón de carpeta, la
aplicación nunca se enteraba, y el siguiente diálogo volvía a abrirse donde ya no
quería guardar. Esa elección cuenta tanto como pulsar el botón, así que ahora se
queda (`str_split_pathname` sobre la ruta devuelta).

Los ajustes se escriben en cuanto cambia algo que debe sobrevivir, no solo al
cerrar: un cierre forzoso no debe costarle al usuario volver a elegirlo todo.

### Dos incoherencias que salieron de camino

`app_save_settings` empezó siendo estática en `ssbapp.c`, así que el botón de
carpeta la llamaba y la orden `folder` no. Eso contradice el principio que el
propio `ssbcmd.c` declara en su cabecera: cada orden hace exactamente lo mismo
que su botón, llamando a las mismas funciones. Ahora es compartida.

Lo mismo con el buffer: `buffer 45` cambiaba el motor pero dejaba el desplegable
marcando otra cosa, así que los ajustes guardaban un valor que el usuario nunca
eligió. Los dos caminos van ahora por `app_set_buffer`, que sincroniza el
desplegable cuando el valor coincide con un preajuste y lo deja quieto cuando no
—mentir marcando otro sería peor que no marcar nada— y el fichero guarda los
segundos de verdad (`buffer_secs`), no un índice.

## Lo que se midió

`probes/run-artefactos.ps1` y `probes/run-carpeta-recordada.ps1`. Este último
hace el recorrido completo: elegir carpeta y formato → cerrar → volver a abrir →
grabar y guardar **sin volver a tocar nada**.

```
savedir=...\build-dbg\elegida
buffer_secs=45
export=1

=== en la carpeta elegida (sin haberla vuelto a elegir) ===
  2026-08-24_14-09-21-1.mp3   247.535 bytes   192 kbps 48000 Hz  MP3 VALIDO
```

Debug y RelWithDebInfo compilan sin un solo aviso; `ctest` pasa en ambas.

## Un tropiezo propio, para que conste

Dos ejecuciones de la prueba de la carpeta dieron «VACIA — la carpeta no se
recordó» cuando el código ya era correcto: `SendKeys` perdía pulsaciones si la
ventana aún no tenía el foco de verdad. Estuve a punto de ponerme a buscar un
fallo que no existía.

La única razón por la que no lo hice es que el fichero de ajustes que sí se
escribía delataba la verdad: contenía los valores **por omisión**, no los que la
prueba creía haber tecleado. Si las órdenes hubieran llegado, habría otra cosa.

Es la cuarta vez en este proyecto (ver `docs/10`). El instrumento de medida sigue
siendo el sospechoso número uno.

Aparte de eso: un reemplazo automático sobre el fuente falló en silencio porque
el patrón llevaba `\n` y llegó convertido en un salto de línea real. El resultado
fue que el guardado escribía la clave `buffer` y la carga esperaba `buffer_secs`
— un ajuste que se guardaba y no se restauraba nunca. Lo cazó la prueba, no la
lectura del código. Los reemplazos que no verifican que han encontrado algo son
una forma estupenda de introducir defectos convencido de haberlos arreglado.
