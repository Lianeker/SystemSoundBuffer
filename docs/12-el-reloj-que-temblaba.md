# 12 — El reloj que temblaba

`docs/11` quitó los chasquidos subiendo una holgura a 5 ms. El usuario volvió con
un fichero donde «todavía se puede sentir algo de artefactos».

Tenía razón, y el arreglo anterior era el correcto sobre un diagnóstico
incompleto. Lo que quedaba no eran chasquidos: era un agujero.

## Lo que había

El fichero era MP3, así que primero hubo que poder mirarlo. Hasta este punto del
proyecto la exportación comprimida solo se validaba por la cabecera — «192 kbps,
48 kHz, 555 tramas» —, que no dice absolutamente nada de las muestras. Un MP3 con
la cabecera perfecta puede llevar silencio. Sin decodificar no se comprueba el
contenido: se cree.

`probes/mfdecode.cpp` decodifica con Media Foundation cualquier cosa que Windows
sepa leer. Con el fichero ya en PCM:

- residuo de predictor normalizado, máximo **1.8**: ni un chasquido. El arreglo
  de `docs/11` aguantaba.
- un **hueco en t=6.470 s**: el nivel cae al 6 % del entorno durante ~5 ms y
  vuelve.

Cinco milisegundos. Exactamente la holgura que había puesto.

## La pregunta que importaba

Un hueco de silencio admite dos explicaciones opuestas, y confundirlas lleva a
arreglar lo que no es:

- **(a)** el audio se perdió de verdad en la captura. Entonces rellenar con
  silencio es lo correcto, no hay nada que recuperar, y lo que habría que
  arreglar es la captura.
- **(b)** el audio estaba entero y el sistema creyó que faltaba. Entonces el
  relleno es espurio y lo que falla es la contabilidad del tiempo.

No hay forma de distinguirlas mirando el audio. Hay que ir al reloj.

## La medición

El motor ya llevaba instrumentación de deriva: por cada paquete guarda el
instante que reporta el sistema y el que sale de contar frames. 30 s de captura
del dispositivo de salida:

```
deriva ms: media -0.633  desv 2.169  min -4.15  max 3.40
   0- 3 s   media -0.982
  27-30 s   media -0.279
```

La media se mueve 0.7 ms en 30 segundos: 23 ppm, la deriva normal entre el reloj
de la tarjeta y el del sistema. **No falta un solo frame.** Pero la desviación
instantánea oscila ±4 ms.

Para no dejarlo en inferencia, `probes/devpos.cpp` va directo a WASAPI y compara
las dos líneas de tiempo que ofrece en cada paquete. 2499 paquetes:

```
paq  frames  dev_delta-frames   qpc_delta_ms  esperado_ms   error_ms
   1     480         -320          5.624       10.000     -4.376
   2     480         -320         15.132       10.000      5.132
   3     480         -320          8.859       10.000     -1.141
   4     480         -320          6.021       10.000     -3.979
   5     480         -320         15.021       10.000      5.021
   6     480         -320          8.879       10.000     -1.121

  posicion de dispositivo: 0 huecos, 0 frames perdidos en total (0.00 ms)
  error del qpc: min -8.456 ms, max 6.230 ms
  banderas: TIMESTAMP_ERROR 0, DATA_DISCONTINUITY 1, SILENT 0
```

La columna de la izquierda no varía **ni una sola vez** en 2499 paquetes: la
posición de muestra del dispositivo avanza siempre igual. Cero pérdida.

La columna de la derecha tiembla, y no al azar: **−4.4, +5.1, −1.1**, y vuelta a
empezar cada tres paquetes, sumando cero. Es un patrón sistemático de la entrega
por lotes, no ruido.

Explicación (b), sin lugar a dudas. La cuenta de frames es exacta; el reloj de
pared es el que miente.

## Por qué el arreglo anterior no podía bastar

Los bloques se colocaban por `t->acc_time`, que es **la marca de un solo
paquete** — con todo su temblor. Dos bloques seguidos llevan cada uno su propio
error independiente, así que su diferencia hereda el ruido:

```
cambio de deriva entre bloques: desv 2.53 ms  max 5.34 ms
   |cambio| > 5 ms:  18 de 351  (5.1%)
   |cambio| > 8 ms:   0 de 351  (0.0%)
```

Con la holgura en 5 ms, el 5.1 % de los límites de bloque la cruzaba y se
rellenaba con silencio. Eso era el agujero.

Y aquí está la trampa en la que era fácil caer: **subir más la holgura no lo
arregla**. El temblor llega a 5.3 ms y el hueco real más pequeño que hemos medido
son 12.7 ms (`docs/03`, las transiciones de flujo del loopback por proceso). Se
solapan demasiado como para separarlos por tamaño. No se puede acertar un umbral
entre dos cosas que se pisan; hay que mirar de dónde viene cada una.

## El arreglo

Los frames dejan de colocarse por la marca de cada paquete. Se cuentan desde un
ancla, que es exacto, y **el ancla solo se rehace cuando falta audio de verdad**.
Dos avisos, ninguno basado en adivinar un número:

1. `AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY`. Es WASAPI diciendo «aquí he perdido
   audio». Apareció 1 vez en 2499 paquetes, así que no salta por ruido.
2. Una red de seguridad por si esa bandera no llega en algún camino: una
   desviación **sostenida**. El temblor vuelve a su sitio en uno o dos paquetes;
   un hueco real no vuelve nunca. Exigir persistencia (3 paquetes seguidos por
   encima de 8 ms) es lo que separa las dos cosas sin tener que acertar un umbral
   entre 4 y 12 ms.

En `ssb_core/src/ssb_track.c`, `i_on_audio`. La holgura de `docs/11` se queda
como segunda línea de defensa en el exportador: con la línea de tiempo lisa ya no
llega a dispararse.

## Lo que se midió después

45 s capturando el loopback **por proceso**, que es el caso difícil — el que de
verdad pierde frames en las transiciones:

```
1 powershell.exe  48000 Hz  3.6 MB  x2.3  buffer 120 s  huecos 0

2026-08-24_14-56-34-1.wav  38.06 s  48000 Hz  pico 11677  rms 1968
interrupciones de la onda (silencio insertado): 0
```

`huecos 0` es el contador de re-anclajes, ahora visible en el listado de pistas:
en 45 s la línea de tiempo no tuvo que rehacerse ni una vez. Y el fichero, que es
WAV sin pérdida, no tiene una sola racha de ceros metida en mitad de la onda.

Ese contador se queda a la vista a propósito. Es lo único que distingue «la
fuente perdió audio» de «todo bien», y si algún día sale distinto de cero, el
silencio que aparezca en la exportación será real y no nuestro.

## Dos veces que el instrumento volvió a mentir

**El detector de ceros exactos.** En el fichero nuevo dio 3208 «artefactos» en
21 s. Estaba contando los silencios digitales reales entre repeticiones del
sonido de prueba.

**El detector de envolvente.** Ya afinado, marcó 31 agujeros en el fichero
arreglado. Antes de creérmelo fui a mirar las muestras:

```
t=2.735 s: -129/-1194  -141/-1195  -147/-1195  -148/-1197
```

Audio normal. El canal izquierdo pasaba por un mínimo mientras el derecho iba a
plena amplitud, y mi detector solo miraba el izquierdo. Cero de esos 31 eran
reales.

Van seis veces en este proyecto (`docs/10`, `docs/11`). El patrón ya no es una
casualidad: **cuando una medición acusa al código, lo primero que hay que auditar
es la medición.**

## Y una que no volverá

Cuatro de esas seis fueron pulsaciones simuladas perdidas porque la ventana aún
no tenía el foco de verdad. Manejar la aplicación con `SendKeys` es un
instrumento poco fiable, y cada vez costó una depuración de un fallo inexistente.

Ahora el programa acepta `--script fichero`: una orden por línea, ejecutada desde
dentro, con `wait <seg>` y `quit`. No depende del foco, no pierde nada y pasa por
exactamente las mismas funciones que los botones. `probes/run-artefactos.ps1` ya
no simula ni una tecla.

Arreglar el instrumento salió más barato que seguir pagándolo.

## Lo que encontro la auditoria del propio arreglo

Un barrido de cuatro auditores sobre el camino de captura, con cada hallazgo
sometido despues a un verificador cuyo trabajo era refutarlo, dejo nueve en pie.
Tres tocaban codigo que yo acababa de escribir:

**El re-anclaje no miraba el signo.** Si la desviacion era negativa, el ancla se
movia hacia atras, y como la holgura de la linea de tiempo (8 ms) es mayor que la
del exportador (5 ms), el desajuste caia fuera del empalme y alli se recortaba:
**descartaba audio bueno**. Y la deriva nominal de esta maquina es negativa
(`docs/03`), asi que no era un caso remoto. Ahora solo se re-ancla hacia
adelante: un reloj que va por detras es deriva, no un hueco.

**La racha contaba las dos direcciones.** Durante una deriva negativa el contador
se quedaba alto, y entonces el primer paquete positivo fuera de banda re-anclaba
sin haber pasado la prueba de persistencia — justo lo que la prueba existe para
impedir. Ahora solo cuenta hacia adelante.

**El hueco aparecia tarde.** `acc_time` solo se toma cuando el acumulador esta
vacio, asi que un salto a mitad de bloque no se veia hasta la frontera del
siguiente, hasta 85 ms despues. Ahora se cierra el bloque antes de saltar.

Ademas, al reanudar tras una pausa la linea de tiempo no saltaba hasta que la
prueba de persistencia lo notaba, y durante tres paquetes el audio nuevo se
pegaba al anterior a la pausa. Ahora el salto se fuerza en el primer paquete.

Y `AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR` no se miraba en ningun sitio del motor.
Cuando WASAPI la pone, el qpc de ese paquete no vale; ahora la captura continua
desde el paquete anterior contando frames en vez de pasar una marca disparatada
que podria provocar un re-anclaje falso. En esta maquina no aparecio ni una vez
en 2499 paquetes, asi que es proteccion para otro hardware, no el arreglo de algo
observado.

Un hallazgo lo deje fuera a proposito. La auditoria proponia usar `devPosition`
para medir el tamano exacto del hueco en vez del QPC, que es lo correcto en
teoria. Pero mi medicion da un desfase constante de -320 frames por paquete que
no se explicar, y el propio verificador senalo que la exactitud de ese contador
es una suposicion sobre WASAPI, no algo que este repo demuestre. Construir sobre
un numero que no entiendo seria repetir el error de todo este documento. Queda
como mejora pendiente, con esa advertencia.

## Un cabo suelto, sin cerrar

En una ejecucion vi `Saved 0 of 1 tracks` y un WAV de 44 bytes — la cabecera sin
muestras. Repetido el caso exacto dos veces mas, salio bien las dos. No se por
que fallo y no lo he podido reproducir. Queda anotado como observado y no
explicado, que es distinto de arreglado.

## La sexta vez, y la ultima

Perseguir el ultimo fallo costo tres diagnosticos falsos seguidos, todos del
instrumento:

1. La prueba dejo de exportar y culpe a mis cambios recien hechos. Era un **BOM
   UTF-8**: `Set-Content -Encoding UTF8` lo escribe, y el guion recibia
   `ï»¿add output`, que no es ninguna orden. El primer comando se perdia
   en silencio. Ahora el cargador tolera el BOM.
2. Un reemplazo automatico sobre el fuente no encontro su patron y no fallo:
   volvi a compilar convencido de haber arreglado el BOM cuando no habia tocado
   nada. Tercera vez que un `
` en un patron llega convertido en salto de linea
   real. Los reemplazos que no comprueban que encontraron algo son una forma
   excelente de introducir defectos creyendo que los arreglas.
3. Una captura de pantalla salio mostrando otra aplicacion que se habia puesto
   encima — llegue a fotografiar una conversacion ajena del usuario. Las
   capturas se han eliminado y las pruebas ya no hacen ninguna.

La leccion se ha convertido en herramienta: el programa escribe ahora su propia
consola en `<guion>.log`. Texto, deterministico, sin foco, sin z-order, sin
pantalla. Las pruebas leen lo que el programa dijo en vez de intentar mirarlo.
