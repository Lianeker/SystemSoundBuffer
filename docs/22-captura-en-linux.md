# 22 — Captura en Linux

`ssb_core/src/linux/ssb_capture_linux.c`, con libpulse. Cubre las tres clases de
fuente:

| Fuente | Cómo |
|---|---|
| Salida del sistema | monitor del sink, `<sink>.monitor` |
| Aplicación concreta | el mismo monitor, acotado a su sink-input con `pa_stream_set_monitor_stream` |
| Entrada | la source directamente |

Es la vía decidida en `docs/01`, sección 6: sin null-sink, sin re-rutear la
aplicación y sin latencia añadida.

## Entorno

WSL2 con Ubuntu 24.04. El servidor de sonido es el de WSLg, que expone un
`RDPSink` a 44100 Hz y su monitor. Que la frecuencia no sea 48000 vino bien:
ejercita el camino que en Windows nunca se recorre.

```
$ ssb list
  salida   output:0    RDP Sink
  entrada  input:0     RDP Source
  app      app:1726    pacat        <- aparece al reproducir algo
```

`probes/linux-captura.sh` monta la prueba entera: reproduce un WAV en bucle,
graba a la vez del monitor y de la aplicación, y saca las cifras.

## Resultados

Con 25 s de audio y las dos fuentes a la vez:

```
[0] RDP Sink     2 ch a 44100 Hz, 24.10 s   discontinuidades 0   pico 1.0000
[1] pacat        2 ch a 44100 Hz, 24.76 s   discontinuidades 0   pico 1.0000
```

La captura por aplicación funciona: la pista de `pacat` trae señal y no la trae
cuando esa aplicación calla.

## El sello de tiempo

El motor coloca por tiempo, así que cada paquete necesita el instante de su
primer frame. WASAPI lo da en `qpcPosition`; PulseAudio no tiene equivalente
directo y hay que derivarlo. Se probaron tres formas, midiendo la frecuencia
efectiva sobre 24 s de audio a 44100 nominales:

| Cómo se calcula el sello | Frecuencia efectiva | Error |
|---|---|---|
| `now`, sin latencia (`pa_stream_get_latency` falla al principio) | 44176 Hz | +0.17 % |
| `now - latencia`, con la foto de tiempos sin refrescar | 22075 Hz | −49.9 % |
| `now - latencia`, con `AUTO_TIMING_UPDATE` e interpolación | 42771 Hz | −3.0 % |
| **`now - duración del paquete`** | **44162 Hz** | **+0.14 %** |

El −49.9 % tiene explicación: sin `PA_STREAM_AUTO_TIMING_UPDATE`,
`pa_stream_get_latency` responde a partir de la última foto de tiempos, y esa
foto no se refresca sola. La latencia crecía un segundo por segundo y el tramo
salía del doble de largo.

Con las banderas puestas la latencia sí se actualiza, pero en el primer paquete
de un monitor no se corresponde con la edad real de esos datos: 700 ms de más.

La cuenta que se usa es `now` menos lo que dura el propio paquete. Queda un
desfase residual de unos 35 ms —lo que el fragmento espera entre capturarse y
llegar al callback—, y ese desfase es **constante**: no estira ni encoge el
tramo, así que no afecta a la alineación entre pistas. Sólo corre 35 ms el
origen absoluto.

La forma de distinguir un error de escala de un desfase constante fue medir dos
duraciones: sobre 3.5 s el error salía +1.16 % y sobre 24 s +0.17 %. En
milisegundos son los mismos 40 en los dos casos.

## Dos cosas que gcc encontró y MSVC no

El motor compila con `/W4 /WX` en Windows y con `-Wall -Wextra -Werror` en
Linux, y el segundo no deja pasar dos cosas que el primero sí:

1. `SSB_TEXT` estaba definida `static` en `ssbtext.h`. Eso da una copia por cada
   `.c` que la incluye, y gcc rechaza las copias que no se usan. La tabla se
   muda a `ssbtext.c` y la cabecera sólo la declara.
2. Una función auxiliar sin usar.

Ninguna de las dos es específica de Linux; es que el aviso allí es un error.

## Lo que falta

- **Reproducción.** Los ocho `ssb_play_*` devuelven `ssb_err_platform`. La
  interfaz ya trata ese error y avisa.
- **MP3 y AAC.** `ssb_encode` ya tenía su stub; la exportación degrada a WAV
  sola.
- **PipeWire nativo.** `pipewire-pulse` implementa el protocolo de PulseAudio,
  así que esto debería funcionar tal cual, pero no se ha probado: en WSLg el
  servidor es PulseAudio de verdad. El riesgo estaba anotado en `docs/01:284`.
- **La lógica de selección de fuente** (`output:2`, `app:firefox`) está escrita
  dos veces, una por backend. Son unas 80 líneas iguales que deberían salir a la
  capa portable.

## Interfaz

NAppGUI compila para Linux con su backend GTK3 y `ssbgui` enlaza y arranca:
añade una pista, ejecuta un guion y sale con 929 de 929 asignaciones liberadas.

No hay captura de pantalla: en esta instalación de WSL el socket X de WSLg no
está montado para el usuario con el que se ejecuta, así que la ventana no llega
a componerse. El programa corre igual, pero **cómo se ve la interfaz en GTK no
está comprobado**.

## Estado

- Linux: `ssb_core`, `ssb` y `ssbgui` compilan sin un aviso. `ctest` verde,
  incluida la sección de mezcla.
- Windows: sin cambios de comportamiento. `ctest` verde en Debug y Release,
  `huecos 0` y 0 interrupciones en 90 s.

Una nota sobre esa última cifra: en una de las pasadas salieron 2 interrupciones
de 16 ms, y el motor había anotado `huecos 2` en la misma pista. Eran cortes
reales de captura con la máquina compilando NAppGUI dentro de WSL. El detector
marca también los huecos que el motor sí vio, porque el motor los rellena con
silencio; por eso `run-artefactos.ps1` imprime ahora las dos líneas juntas. La
que importa es una interrupción **sin** su hueco correspondiente.
