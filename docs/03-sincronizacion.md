# Los 80 ms: qué eran y cómo se arreglaron

Fecha: 2026-08-24. Cierra el riesgo nº1 de [`01-viabilidad-y-decisiones.md`](01-viabilidad-y-decisiones.md) §8.

## El síntoma

La primera grabación en vivo dejó un número que no cuadraba: el loopback por
proceso reportaba **80 ms** de desajuste entre el instante que informa el sistema
y el que sale de contar frames, mientras la salida del sistema daba 0.45 ms y el
micrófono 0.26 ms. 80 ms sobre 15 s son 0.53 %: dos órdenes de magnitud más de lo
que deriva un cristal de cuarzo. Algo estaba mal, y no era el reloj.

## Cómo se investigó

Con un solo valor final no se puede diagnosticar nada: un desfase de arranque y
una deriva de reloj dan el mismo número al final de la grabación. Así que lo
primero fue instrumentar **la serie completa**, una muestra por paquete
(`ssb_track_drift`, unas 2500 muestras en 25 s), y añadir `ssb drift` para
volcarla a CSV.

### Primera medida: no es lo que parecía

| Fuente | Forma | Máximo |
|---|---|---|
| salida del sistema | plana | 1.70 ms |
| micrófono | plana | 1.56 ms |
| **loopback por proceso** | **irregular, 8 saltos** | **102.59 ms** |

El perfil por deciles del loopback por proceso fue `0 → 53 → 92 → 32 → 74`: subía
y bajaba. Ni escalón ni pendiente.

Al mirar el CSV apareció el detalle que lo explicaba: **hubo exactamente una
bajada brusca, y exactamente una muestra por encima de 100 ms**. Los 100 ms eran
el umbral de mi propio relleno de huecos. Es decir: el desajuste **no** se
recuperaba solo, crecía de forma sostenida, y la única vez que "bajó" fue mi
propio código inyectando ~100 ms de silencio fantasma. El parche estaba
enmascarando el síntoma que había que leer.

### Segunda medida: la que discriminó

Quedaban dos explicaciones incompatibles, y el emisor de prueba las confundía:
reproducía un WAV corto **13 veces seguidas**, abriendo y cerrando el flujo de
render cada vez.

- (a) desajuste real de frecuencia → el déficit crece también con un flujo continuo
- (b) pérdida en cada transición → con un flujo continuo desaparece

Así que se generó un tono de 40 s y se reprodujo **una sola vez**, sin reabrir el
flujo (`probes/run-drift-continuo.ps1`):

| Emisor | Frecuencia efectiva | Déficit |
|---|---|---|
| 13 arranques/paradas | 47705.4 Hz (**−0.614 %**) | 165.5 ms en 27 s |
| **flujo continuo** | **48019.6 Hz (+0.041 %)** | −10.6 ms en 26 s |

+0.041 % es exactamente lo que dan la salida del sistema (+0.037 %) y el
micrófono (+0.040 %) en la misma máquina.

## La causa

**El loopback por proceso sigue el mismo reloj que todo lo demás. Lo que hace es
perder unos 12.7 ms de frames en cada apertura o cierre del flujo de render de la
aplicación objetivo.**

No es deriva. Es pérdida discreta en las transiciones, que se acumula
indefinidamente si la app arranca y para audio muchas veces. Para el caso de uso
esto importa mucho: una llamada de WhatsApp es un flujo continuo y no sufre, pero
**una sucesión de notas de voz abre y cierra el flujo una vez por nota**, y cada
una desplaza todo lo que viene después.

## El arreglo

**La línea de tiempo pasa a ser la que reporta el sistema, no la cuenta de
frames.** Cada bloque guarda el instante real de su primer frame
(`ssb_track.acc_time`, tomado del `qpcPosition` del paquete). Contar frames era
correcto solo mientras ninguna fuente perdiera ninguno.

Consecuencias:

1. **Fuera el relleno de huecos por cuenta de frames.** Era un parche sobre un
   diagnóstico equivocado. Con marcas de tiempo reales el hueco se representa
   solo, y lo rellena quien exporta, donde de verdad corresponde.
2. **La exportación coloca cada bloque en su instante real** y rellena con
   silencio los huecos que dejó la fuente (`ssb_ring_save_wav`). El WAV dura
   exactamente el tramo pedido. El silencio se inserta justo donde la aplicación
   había dejado de sonar, así que es inaudible.
3. **`ssb_track_stats.eff_rate`** expone la frecuencia efectiva real. Si se aleja
   del nominal, la fuente está perdiendo frames y se ve en el acto.
4. **El CLI guarda todas las pistas sobre la ventana común** (la intersección de
   sus tramos), que es lo que hace que los WAV sean comparables entre sí.

## La verificación

Grabación de 60 s con el emisor a ráfagas (~30 transiciones), midiendo el desfase
real entre la pista de la salida del sistema y la del loopback por proceso por
correlación cruzada de envolventes:

```
la fuente perdio 394 ms de frames en 60 s (-0.657 %)

desfase por tercios:
   t= 0-20s: -5.33 ms   corr 0.766
   t=20-40s: -5.33 ms   corr 0.785
   t=40-60s: -6.00 ms   corr 0.772
```

**Constante dentro de la resolución de medida** (0.67 ms). Sin el arreglo, el
último tercio habría estado ~394 ms fuera de sitio y creciendo. Los −5.3 ms
residuales son latencia real de tubería: el tap por proceso y el loopback de
dispositivo no pinchan en el mismo punto de la mezcla. Es constante, no se
acumula, y si algún día molesta se compensa con un desplazamiento fijo.

## Pruebas de regresión añadidas

En `ssb selftest`, sobre un ring con un hueco deliberado de 0.5 s:

- la duración del WAV es la del tramo pedido (3.9133 s pedidos → 3.9133 s)
- se rellena justo el hueco (0.5000 s)
- un recorte interior arbitrario de 1.0000 s sale de 1.0000 s exactos

## Lo que queda abierto

- **No se sabe si los 12.7 ms por transición son propios de esta máquina.** El
  número saldrá distinto en otro hardware; lo que no debería cambiar es que
  existan.
- **Ninguna fuente se ha resampleado.** Con la línea de tiempo por reloj no hace
  falta mientras las pérdidas sean discretas. Si alguna vez aparece una fuente
  con desajuste **real** de frecuencia (una tarjeta a 44.1 kHz reportada como 48,
  por ejemplo), haría falta un resampler y esto se vuelve a abrir.
- **Los −5.3 ms de latencia entre taps no se han caracterizado** en más de una
  máquina ni con más de un par de fuentes.

## La lección

El primer número sospechoso venía de un código que ya estaba corrigiendo el
problema a ciegas. El relleno de huecos convertía una pérdida sostenida en un
diente de sierra, y el diente de sierra hacía ilegible el diagnóstico. **Antes de
medir algo, hay que quitar del camino los parches que ya lo estaban tocando** — o
al menos instrumentarlos para saber cuándo actúan.

Y la segunda: **una medida que no discrimina entre dos hipótesis no vale**. La
primera tanda de datos era abundante y correcta, y aun así no decidía nada,
porque el emisor de prueba confundía las dos explicaciones. El experimento que
sirvió fue el que cambió una sola variable.
