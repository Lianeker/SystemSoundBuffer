# 13 — Calidad, mezcla y escucha

Cuatro peticiones que resultaron estar mas relacionadas de lo que parecia:
independencia del volumen, maxima resolucion, exportar todo en un fichero, y
reproducir lo seleccionado.

## 1. ¿Depende la captura del volumen de salida?

No. Y no es una opinion: `probes/volumen.cpp` pone el volumen maestro a tres
valores y mide la misma senal —un seno de 440 Hz a −6 dBFS exactos, generado
para la prueba— en las dos formas de captura.

```
volumen  | loopback del DISPOSITIVO           | loopback por PROCESO
   100%  | rms   -9.08 dB  pico   -5.55 dB    | rms   -9.08 dB  pico   -6.02 dB
    50%  | rms   -9.08 dB  pico   -5.68 dB    | rms   -9.08 dB  pico   -6.02 dB
    20%  | rms   -9.08 dB  pico   -6.05 dB    | rms   -9.08 dB  pico   -6.02 dB
```

El instrumento se valida solo: un seno con pico a −6 dBFS tiene que dar −9.01 dB
de valor eficaz, y midio −9.08. Si esa cifra no hubiera salido, la tabla entera
seria sospechosa.

Dos conclusiones:

- **El volumen del sistema es irrelevante** para lo que se graba, en las dos
  vias. El control esta despues de donde pincha el loopback.
- **El loopback por proceso es mas fiel.** Devuelve el pico exacto de origen
  (−6.02 dB) a los tres volumenes, mientras que el del dispositivo se desvia
  hasta medio decibelio porque pasa por la mezcla del endpoint. Para conservar
  calidad, capturar la aplicacion es mejor que capturar la salida.

Eso responde tambien a lo de WhatsApp: si su audio "no sale por el canal
seleccionado" es porque esta renderizando a otro dispositivo. Capturarla por
proceso evita el problema de raiz, porque no depende de por donde salga.

## 2. La lista de canales que van al sistema

Antes solo se sabia que aplicaciones tenian sesion de audio. Ahora se dice **por
donde suena cada una y si suena ahora mismo**:

```
> list
output:0  Headphones (Bose QC Ultra 2 Earbuds)
output:1  Speakers (Realtek(R) Audio)
input:0   Headset (Bose QC Ultra 2 Earbuds)
app:6936  WhatsApp.Root.exe      SUENA  -> Headphones (Bose QC Ultra 2 Earbuds)
app:6428  Spotify.exe            callada -> Headphones (Bose QC Ultra 2 Earbuds)
```

No tiene nada de artesanal: sale de `IAudioSessionManager2`, que es la API que
Windows expone justo para esto, y el enumerador ya recorria los dispositivos —
solo se estaba tirando el dato de cual era. El estado viene de
`IAudioSessionControl::GetState`.

Sirve para la pregunta que uno se hace de verdad: *¿por que grabo silencio?*
Antes habia que averiguarlo probando.

## 3. La resolucion: donde estaba el techo real

WASAPI entrega **float32**. El motor lo cuantizaba a **int16** sin preguntar
(`ssb_track.c`, la conversion `v * 32767.0`). Ese era el techo, y no tenia nada
que ver con el volumen ni con el codec: era una decision tomada una vez y nunca
revisada.

Ahora hay cuatro modos en el desplegable de almacenamiento: sin perdida o crudo,
a 16 o a 24 bits. 24 bits es el maximo util —la mantisa de un float32 son 24
bits, asi que por encima no hay nada que conservar.

**Lo que habia que demostrar no era que funcionara, sino que no arruinara el
buffer.** Un circular de horas depende de la compresion. Medido, grabando lo
mismo en las dos profundidades:

| | disco | ratio | WAV exportado |
|---|---|---|---|
| 16 bits | 0.9 MB | **x4.3** | 3.850.284 bytes |
| 24 bits | 1.7 MB | **x4.3** | 5.775.404 bytes |

Exactamente 1.5× de disco, y **el mismo ratio de compresion**. La razon es que
el predictor y el codigo Rice ya trabajaban en `int32` por dentro: subir la
resolucion solo agranda los residuos, no desactiva nada. Fue suerte de un diseno
anterior, no merito de este cambio.

¿Y los 8 bits de mas llevan algo? Medido sobre una grabacion real a 24 bits:

```
octetos bajos distintos de cero: 97.3% de las muestras
valores distintos en los 8 bits bajos: 256 de 256
```

Los 256 valores posibles aparecen. La mezcla del sistema trabaja en float y
produce mas de 16 bits de informacion real, asi que la opcion no es decorativa.
Dicho esto: si la fuente es voz ya comprimida, esos bits llevaran sobre todo el
ruido de la propia mezcla. 24 bits garantiza no perder nada; no inventa nada.

### Un fallo que solo aparecio al subir la resolucion

El escape "constante" del codec —el que hace que un bloque de silencio quepa en
6 bytes— guardaba el valor en **20 bits**. Sobra para int16, pero el canal
lateral es `l - r` y a 24 bits llega al doble de la escala completa: 25 bits de
zigzag. Truncarlo producia un bloque que **decodificaba mal, en silencio**.

Lo cazo el autotest nuevo comparando muestra a muestra, no la lectura del
codigo. El campo pasa a 26 bits y, si aun asi no cupiera, se usa el camino
normal en vez de escribir un valor equivocado: mejor gastar bytes que mentir.

Es el argumento entero a favor de escribir la prueba antes de dar por buena la
ruta nueva. Sin ella, esto se habria manifestado como audio corrupto ocasional a
24 bits, meses despues, con silencio de por medio.

## 4. Exportar todas las pistas en un fichero

`ssb_mix_wavs` suma varios WAV en uno. Lo importante es **donde** esta:
*despues* de la exportacion normal, no en paralelo. Cada pista se escribe con el
camino de siempre —ventana comun, huecos, recorte, silenciadas— y la mezcla solo
suma los resultados. Asi el fichero mezclado no puede sonar distinto de lo que
sale por separado, que era el error facil de cometer aqui.

Dos pasadas: la primera solo mide el pico de la suma, la segunda escribe con la
ganancia justa. Recortar al vuelo distorsionaria; dividir por el numero de pistas
bajaria el volumen aunque no hiciera falta. La ganancia aplicada se dice en el
mensaje (`ganancia x1.000` cuando no hizo falta ninguna), porque una ganancia
silenciosa es una sorpresa esperando a pasar.

La profundidad de la mezcla es la **mayor** de las entradas: bajarla tiraria
bits que alguien pidio expresamente conservar.

## 5. Reproducir lo seleccionado, con pausa

`ssb_play_win.cpp`: WASAPI en modo render, hilo propio, prioridad de audio por
`AvSetMmThreadCharacteristics` —sin eso un hilo de reproduccion se queda sin
turno bajo carga y se oyen cortes que no estan en el fichero.

La decision de diseno que importa: **se reproduce el fichero ya exportado**, no
una segunda lectura del anillo. Podria ahorrarse el fichero temporal, pero
entonces habria dos caminos distintos hacia el mismo audio y nada garantizaria
que suenan igual. Reproducir lo exportado significa que escuchar es, por
construccion, una comprobacion de lo que se guarda.

Pausar no cierra el flujo: solo deja de alimentarlo, para que seguir sea
instantaneo. Y hay una cabeza de reproduccion sobre la onda, porque con una
seleccion larga no saber por donde va se nota enseguida.

## Lo que se midio

`probes/run-nuevo.ps1`, entero por guion y sin una sola pulsacion simulada:

```
> list          -> WhatsApp.Root.exe SUENA -> Headphones (...)
> quality 24    -> resolucion: 24 bits
> mix on        -> exportar: todo en un fichero
> save          -> Mixed 2 tracks into one file, 14.14 s  (gain x1.000)
> play          -> reproduciendo (14.14 s)
> play          -> pausa
> play          -> reproduciendo
> hush          -> reproduccion parada
1  Headphones (...)  48000 Hz  1.2 MB  x4.4  buffer 120 s  huecos 0
2  powershell.exe    48000 Hz  2.2 MB  x2.3  buffer 120 s  huecos 0
```

El fichero mezclado: 24 bits, 2 canales, 48 kHz, 14.14 s, pico 0.43 de escala
completa. Y los artefactos siguen a cero tambien a 24 bits: 64 s de audio en dos
fuentes, **0 interrupciones**.

Debug y RelWithDebInfo compilan sin un aviso; `ctest` pasa en las dos.

## Lo que NO se hizo, y por que

La auditoria del capitulo anterior proponia usar `devPosition` de WASAPI para
medir el tamano exacto de los huecos. Sigue siendo lo correcto en teoria, pero la
medicion da un desfase constante de −320 frames por paquete que no se explicar, y
el verificador de la propia auditoria senalo que la exactitud de ese contador es
una suposicion sobre WASAPI, no algo que este repositorio demuestre.

Construir sobre un numero que no se entiende es exactamente el error que
`docs/12` documenta. Queda pendiente, con esa advertencia escrita.
