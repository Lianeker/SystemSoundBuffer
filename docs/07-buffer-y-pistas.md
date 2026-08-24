# Buffer en caliente, botones de pista y el final que se perdia

Fecha: 2026-08-24. Continua [`06-exportacion.md`](06-exportacion.md).

## El final de la grabacion se estaba perdiendo

Sintoma reportado: con un buffer corto, se hace sonar algo breve, se para en
cuanto termina, se selecciona hasta el final y **el sonido no esta entero**.

Era real, y por dos caminos distintos:

1. **Al parar.** `ssb_track_pause` cortaba por bandera: el siguiente paquete que
   llegaba se tiraba. Pero el cliente WASAPI tiene hasta **200 ms de audio ya
   capturado** esperando a que lo recojamos. Cortar en seco lo tiraba.
2. **Al cerrar una pista.** El hilo de captura salia del bucle y llamaba a
   `Stop()` sin leer lo que quedaba pendiente. Mismo efecto.

Corregido en los dos sitios, y de forma distinta en cada uno porque el problema
no es el mismo:

- **La pausa se hace por instante, no por bandera.** `ssb_track_pause` guarda
  `pause_at = ssb_now()` y el callback acepta todo paquete cuyo `qpc` sea
  anterior a ese instante. Lo que ya estaba capturado entra; lo posterior, no.
  El corte es exacto en el momento en que se pulso Parar, ni antes ni despues.
- **El hilo de captura vacia lo pendiente antes de salir.** Un ultimo barrido de
  `GetNextPacketSize`/`GetBuffer` despues del bucle y antes de `Stop()`.

Verificado con `probes/run-gui-cola.ps1`: se graba, suena un pitido, se para
inmediatamente despues, se selecciona todo y se guarda.

```
2026-08-24_12-33-21-1.wav: 2 ch, 48000 Hz, 165946 frames = 3.457 s
audio audible de t=2.35 s a t=2.95 s
silencio despues del sonido: 0.50 s
ultimas 6 ventanas de 50 ms: [1, 0, 0, 0, 0, 0]
-> EL SONIDO TERMINO ANTES DE PARAR: no se corto nada
```

El fichero se extiende medio segundo mas alla del sonido. Antes terminaba justo
donde el sonido seguia.

## Cambiar el buffer sin parar de grabar

El desplegable de duracion solo afectaba a las pistas **nuevas**. Cambiarlo con
la grabacion en marcha no hacia nada visible, y tampoco cambiaba nada en memoria:
las pistas ya creadas seguian con su anillo del tamano original.

Ahora `ssb_track_set_buffer()` lo cambia **en caliente**:

- **Reducir** descarta lo que sobra por el principio en el siguiente bloque.
- **Ampliar** deja crecer el buffer desde ese momento.
- El mapa de picos se reasigna para cubrir lo mismo que el audio, conservando los
  picos mas recientes. Si no, la interfaz volveria a tener menos picos que audio
  y la onda vieja bailaria otra vez (el defecto de docs/05).

Prueba de regresion en `ssb selftest`:

```
(con 20 s pedidos, el buffer cubre 21.16 s)   -> arranca con la duracion pedida
(tras reducir a 5 s, cubre 4.52 s)            -> el buffer se encoge de verdad
(tras ampliar a 15 s, cubre 15.45 s)          -> el buffer vuelve a crecer
lo que queda sigue siendo identico            -> ok
```

## Botones por pista

Cada banda lleva ahora dos botones dibujados a su izquierda: **M** para
silenciar y **X** para cerrar esa pista concreta, no solo la ultima. Al cerrar
una del medio, las siguientes se renumeran y recolorean.

Van dibujados en el lienzo y no como controles nativos por dos razones: no se
pueden meter controles dentro de un `View`, y ademas tienen que desplazarse con
la lista al hacer scroll.

## Nombre de fichero y carpeta

El nombre por omision es **la fecha y hora reales del principio del tramo
seleccionado**: `2026-08-24_12-33-21-1.wav`. Un fichero llamado `selection-3` no
dice nada dentro de un mes.

Para eso hizo falta anclar la linea de tiempo a la hora de pared:
`ssb_wall_clock()` fija una referencia entre QPC y el reloj del sistema la
primera vez que se le llama, y a partir de ahi todo se deriva por diferencia. Asi
un instante del buffer se traduce a una hora concreta sin depender de que el
reloj no se haya movido entretanto.

Y dos botones: **Folder...** abre `comwin_select_dir` para elegir donde guarda
Ctrl+S, y **Default** vuelve a la carpeta de por omision.

Nota de proceso: el tooltip del boton de carpeta llego a decir "con el boton
derecho vuelve a la carpeta por omision", que era una promesa que el codigo no
cumplia — `button_OnClick` solo responde al boton izquierdo. En vez de dejar el
texto, se implemento el boton. Un tooltip que miente es peor que no tenerlo.
