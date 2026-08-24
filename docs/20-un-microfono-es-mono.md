# 20 — Un micrófono es mono

Dos síntomas que parecían no tener nada que ver:

- añadir una pista a mitad de la grabación y ya no poder **reproducir** lo
  seleccionado, aunque lo exportado estuviera bien;
- pedir «un solo fichero» y recibir **varios, y distintos**.

Eran el mismo fallo.

## La reproducción y la exportación mezclada usan el mismo mezclador

Reproducir no es leer los buffers: se escribe cada pista a WAV, se suman en uno
y **ese** es el que suena. Es a propósito — reproducir cuatro ficheros a la vez
sería un segundo mezclador, y ya hay uno. Así que cuando el mezclador se niega,
se caen las dos cosas a la vez.

Y el mezclador se negaba aquí, `ssb_core/src/ssb_mix.c`:

```c
/* Todo lo que se mezcla sale del mismo tramo comun y del mismo motor, asi que
   discrepar aqui significa que algo va mal aguas arriba: mejor negarse que
   mezclar cosas desalineadas. */
if (in[i].channels != ch || in[i].rate != rate)
    return ssb_err_arg;
```

El comentario era razonable y la premisa era falsa. **Un micrófono es mono y una
salida es estéreo.** Grabar los dos a la vez no es «algo que va mal aguas
arriba»: es el caso normal. En cuanto añadías un micrófono, ya no se podía
juntar en un fichero ni escuchar nada.

Reproducido en 30 segundos:

```
> add output:0        Headphones (Bose QC Ultra 2 Earbuds)   estereo
> rec ... > add input:1   Microphone Array (AMD Audio Device)    MONO
> stop > all > save
[msg] That selection falls outside what every track covers.
=== ficheros ===
  2026-08-24_20-25-51-1.wav   2.532.412      <- estereo
  2026-08-24_20-25-51-2.wav   1.266.226      <- la mitad justa: mono
```

Ahí están los dos síntomas de una vez, y ahí está la pista: el segundo fichero
mide **exactamente la mitad** del primero.

## Por qué salían varios ficheros

La exportación mezclada escribe primero una pista por fichero y luego las suma:

```c
if (ssb_mix_wavs(plist, nparts, one, &gain) != ssb_ok)
{
    i_say(app, T(app, TXT_MSG_OUT_OF_RANGE));
    return;                     /* <- los trozos se quedan */
}
for (k = 0; k < nparts; ++k)
    remove(plist[k]);
```

Al fallar la suma, el `return` se llevaba por delante la limpieza. Los trozos
por pista se quedaban en la carpeta: **varios ficheros, y distintos**, que es
literalmente lo que se veía. Y el mensaje hablaba de la selección, que no tenía
nada que ver.

## Qué se ha hecho

**1. La mono se reparte, no se rechaza.** La salida lleva los canales del que
más tenga y una entrada mono va **a todos los canales** por igual, que es lo que
hace cualquier mesa de mezclas. Lo único que se sigue rechazando es mezclar
frecuencias distintas: juntar 44100 con 48000 sin remuestrear pondría una de las
dos a otra velocidad. Eso ahora devuelve un error propio, `ssb_err_format`, para
que quien llame pueda **decir qué ha pasado**.

**2. Los trozos se quedan a propósito, y se dice.** Si las pistas no caben en un
fichero por la frecuencia, los ficheros por separado **son exportaciones
válidas** y es mejor darlas que no dar nada. Lo que faltaba era decirlo:

> Las pistas van a frecuencias distintas y no caben en un solo fichero. Se han
> guardado 2 ficheros por separado: …-N.wav

**3. Los mensajes dicen la causa.** Cualquier fallo del mezclador salía como
«esa selección cae fuera de lo que cubren todas las pistas» — un mensaje que
mandaba a buscar el problema donde no estaba. Es la segunda vez que ese mismo
mensaje tapa otra cosa (`docs/17`), y la lección se repite: **un mensaje de
error que no distingue entre causas no informa, desinforma.**

## Un desfase que aún no había mordido

Ya puestos en el bucle de mezcla, había esto:

```c
for (i = 0; i < n; ++i)
{
    uint32_t got = i_wavr_read(&in[i], buf, want);
    if (got < want)
        want = got;              /* want encoge A MITAD del bucle */
    for (k = 0; k < want * ch; ++k)
        acc[k] += ...;
}
```

Si una entrada se queda corta, `want` encoge — pero las entradas **anteriores ya
habían avanzado su fichero** los `want` frames grandes. A partir de ese bloque
van desplazadas, para siempre. No se había visto porque todas las entradas
vienen del mismo tramo y hasta ahora medían lo mismo.

Ahora se leen **todas** primero, y solo después se suma el tramo que todas
cubren.

## La prueba que faltaba

El mezclador tenía dos fallos reales y **ninguna prueba**. Ahora hay una sección
de `selftest`:

```
== mezcla ==
  escribir un WAV estereo                                    ok
  escribir un WAV mono                                       ok
  mono + estereo se pueden mezclar                           ok
  la mezcla sale con los canales del que mas tiene           ok
  la mezcla dura lo mismo que las entradas                   ok
  no hizo falta bajar la ganancia                            ok
  cada muestra es la suma exacta, con la mono en los dos canales  ok
  escribir un WAV a 44100                                    ok
  frecuencias distintas se rechazan con ssb_err_format       ok
```

Dos decisiones de esa prueba que importan:

- **10 000 frames**, más de dos bloques de 4096. El desfase del apartado
  anterior solo aparece cruzando bloques; con un bloque no se ve.
- La cabecera WAV se escribe **a mano**, no con el escritor del motor. Si la
  prueba usara nuestro escritor comprobaría que el mezclador sabe leer lo que
  nosotros escribimos, que no es lo mismo que saber leer un WAV.

Y no depende de que haya un micrófono conectado, que es justo lo que hizo falta:
el micrófono de esta máquina grabó **silencio absoluto**, así que la
comprobación contra hardware real («la mezcla es la suma de las partes») salía
verdadera por sumar ceros. Una prueba que pasa sumando ceros no prueba nada.

## Medido

Con la misma selección exportada de las dos formas, a la vez:

```
por separado:  ...-1.wav  2.490.412   ...-2.wav  1.245.226 (mono)
en un fichero: ...wav     2.490.408
```

```
pista 1: 622592 frames, 2 canales   pista 2: 622591 frames, 1 canal
mezcla:  622591 frames, 2 canales
maxima diferencia con la suma esperada: 0
```

Cero. La mezcla es **exactamente** la suma de las partes, con la mono en los dos
canales.

Y lo demás sigue en pie: `ctest` verde en Debug y Release, la exportación por
separado con una pista añadida tarde sigue dando dos ficheros de la misma
duración, y los artefactos de audio siguen a **0 interrupciones en 94 s**.

**Sin comprobar de punta a punta:** el mensaje de «frecuencias distintas» tal y
como se ve en la interfaz. Los cuatro dispositivos de esta máquina van a 48000,
así que no hay forma de provocarlo sin cambiarle a alguien la configuración de
sonido de Windows. Lo que sí está probado es que el motor lo detecta y devuelve
`ssb_err_format`.
