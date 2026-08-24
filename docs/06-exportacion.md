# Exportacion comprimida, idioma y silenciado

Fecha: 2026-08-24. Continua [`05-ajustes.md`](05-ajustes.md).

## Exportacion a MP3 y AAC, sin dependencias externas

**Se puede, y esta ejecutado.** Windows trae el codificador: `MFTEnumEx` sobre
`MFT_CATEGORY_AUDIO_ENCODER` lista nueve en esta maquina, entre ellos
*MP3 Encoder ACM Wrapper MFT*, *Microsoft AAC Audio Encoder MFT*, y de propina
*Microsoft FLAC* y *ALAC*. Las unicas DLL que arrastra el ejecutable son MFPlat,
MF, MFReadWrite y ole32, todas de System32.

Camino elegido, de los cinco que se probaron: `MFCreateSinkWriterFromURL` con la
extension del fichero de salida. Es el mas simple y el que no tiene la trampa
descrita abajo.

Medido sobre un WAV real del propio motor (1 378 920 bytes, 7.18 s, 48 kHz
estereo):

| Formato | Tamano | Ratio | Comprobado |
|---|---|---|---|
| MP3 192 kbps | 173 807 B | 7.9:1 | cabecera de frame, 48 000 Hz |
| M4A (AAC) 128 kbps | 117 570 B | 11.7:1 | cajas `ftyp`/`mdat`/`moov`, 48 000 Hz |

Y de extremo a extremo desde la interfaz, con tres pistas y Ctrl+S:

```
selection-1-1.mp3   20015 B  192 kbps  48000 Hz  34 frames = 0.82 s  VALIDO
selection-1-2.mp3   20015 B  192 kbps  48000 Hz  34 frames = 0.82 s  VALIDO
selection-1-3.mp3   13487 B  128 kbps  48000 Hz  34 frames = 0.82 s  VALIDO
```

Misma duracion exacta en las tres: **la alineacion multipista sobrevive a la
transcodificacion**, que era lo unico que podia estropearla.

### La trampa que hay que conocer

Con `MFCreateMP3MediaSink`, si no se fija el tipo en el `IMFMediaTypeHandler` del
stream sink, **todo devuelve `S_OK` y el fichero sale remuestreado en silencio a
32 kHz**. No hay ningun error: simplemente has perdido la banda por encima de
16 kHz. El sink nace sin tipo y el que eligieras con
`MFTranscodeGetAudioOutputAvailableTypes` se ignora.

Por eso `ssb_encode` hace dos cosas: usa el camino por URL, que no lo sufre, y
**reabre el fichero al terminar para comprobar la frecuencia**. Si no coincide
con la fuente, devuelve error en vez de entregar algo degradado en silencio.

### Por que se transcodifica desde el WAV

`ssb_encode` toma un WAV ya escrito en vez de codificar desde el anillo. Es
deliberado: el camino de exportacion (ventana comun entre pistas, relleno de
huecos, recorte exacto al frame, pistas silenciadas) ya esta verificado y es uno
solo. Meter un segundo camino que hiciera lo mismo por dentro habria duplicado
todo eso. Si la codificacion falla, **el WAV se queda**: mejor eso que nada.

En Linux no hay codificador del sistema, asi que `ssb_encode` devuelve
`ssb_err_platform` y la interfaz ofrece solo WAV. La eleccion de formato esta en
el desplegable **Export** y en `ssb encode` del CLI.

## Idioma

Interfaz en ingles por omision, con boton **ES/EN** que cambia en caliente todo:
botones, etiquetas, desplegables, HUD, mensajes y nombres de pista. Tambien
`--lang en|es`.

La tabla esta en `ssbtext.h`: dos idiomas por identificador, plana y sin
dependencias. NAppGUI trae su sistema de recursos (nrc), pero para dos idiomas y
cuarenta cadenas es mas maquinaria de la que hace falta, y asi el cambio no
recarga nada.

De paso se quitaron las coletillas de los avisos: ahora son `CAPTURE STOPPED` y
`VIEW FROZEN`, secos.

**Efecto lateral que valia la pena:** los nombres de fuente por omision venian
del motor y en espanol ("dispositivo de salida predeterminado"). Ahora el motor
resuelve el nombre real del dispositivo, asi que la interfaz ensena
`Speakers (Realtek(R) Audio)`. El motor ya no genera texto visible.

## La rueda hace scroll

Estaba mal: la rueda hacia zoom y la lista de pistas no se podia desplazar.
Ahora **rueda = scroll**, y el zoom pasa a **Ctrl+rueda** y a los botones −/+.
Verificado: `vp.y` avanza 20 → 127 → 234 → 321 y se detiene en el tope correcto
con cuatro pistas.

Detalle de implementacion: `EvWheel` no trae modificadores, pero `EvMouse` si.
El estado de Ctrl se cachea en cada movimiento del raton y la rueda lo consulta;
si alguna vez esta desfasado, cae del lado seguro y hace scroll.

## Silenciar pistas

Se pulsa el nombre de una pista y se silencia: marca `[MUTED]`, onda apagada,
vumetro apagado. **Al exportar, las silenciadas no se escriben.** El mensaje
cuenta solo las activas, y si estan todas silenciadas avisa en vez de generar
ficheros vacios.

## Un hallazgo que no se buscaba: la salida del sistema estaba muda

Probando lo anterior aparecieron pistas de salida marcando `0.0 MB x2340` —
silencio puro— mientras la de aplicacion captaba bien. Aislado con la sonda
independiente, fuera del motor, daba lo mismo. La causa:

```
salida predeterminada : Speakers (Realtek(R) Audio)
volumen maestro       : 0 %
silenciado            : SI
```

El loopback de dispositivo captura la mezcla del endpoint, que es silencio con el
sistema mudo. El loopback por proceso pincha antes de la mezcla y por eso si
capta. **No es un defecto del codigo, pero si de producto**: grabas silencio sin
saber por que, y la onda plana no lo distingue de "no habia sonido".

Ahora `ssb_output_muted()` consulta el endpoint y cada pista de salida avisa en
su banda: *SYSTEM OUTPUT IS MUTED - this track is recording silence*. Se
comprueba una vez por segundo.

Es lo unico que se hizo sin haberlo pedido. Se justifica porque, sin eso, el
fallo mas facil de la funcion principal es invisible.


## Grabar cuando se diga, no al anadir la pista

Antes, anadir una pista arrancaba su captura al instante. Ahora una pista recien
anadida **esta lista pero no graba**: se monta el conjunto de fuentes con calma y
se pulsa **Record**. El boton alterna Record/Stop (Ctrl+P), y el estado se ve de
un vistazo con un **punto rojo dibujado en el lienzo** — los botones nativos de
Windows no se pueden colorear, pero el lienzo es nuestro.

Parado, los buffers quedan intactos: es lo que permite mirar y guardar sin que lo
que buscas se salga del circular por atras.

## Dos defectos de interfaz, y por que ocurrieron

**Los botones salian todos del mismo tamano y con el texto recortado.** Los
rotulaba en `app_relabel()`, que corre DESPUES de componer el layout. NAppGUI
calcula el ancho de cada columna con el contenido que hay en ese momento, y con
los botones vacios todas las columnas salian iguales. Ahora los textos se ponen
en `i_toolbar()`, antes de componer, y al cambiar de idioma se llama a
`layout_update()` para recomponer con los anchos nuevos.

**El scroll no movia nada.** Dibujaba las bandas en coordenadas del viewport
(`top + TOP_H + i * LANE_H`) para poder anclar la regla y el HUD arriba. El
efecto era que TODO seguia al scroll y la vista parecia congelada: `view_scroll_y`
funcionaba, pero no se notaba. Ahora las bandas van en **coordenadas de
contenido** (`TOP_H + i * LANE_H`) y solo la regla y el HUD se anclan al
viewport. Verificado con cuatro pistas: tras seis muescas de rueda se ven la 3 y
la 4, que antes eran inalcanzables.

La leccion: en una vista desplazable hay dos sistemas de coordenadas y hay que
tener claro cual usa cada cosa. Anclar de mas es tan roto como anclar de menos.
