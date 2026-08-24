# 14 — La onda que bailaba, y qué falta

## 0. Primero, un destrozo mío

La sonda `probes/volumen.cpp` cambia el volumen maestro de la máquina para medir
si la captura depende de él. Restauraba el valor original **en su última línea**.

Una ejecución murió a mitad y dejó el volumen donde estuviera. Resultado: el
usuario estuvo oyendo WhatsApp al 20 % sin saber por qué, y lo achacó al
programa. Medido con `probes/vol.cpp`, el maestro estaba efectivamente al 20 %;
se devolvió a 74 %, que era el último valor suyo que quedó registrado.

Dos cambios, y el segundo importa más que el primero:

1. `volumen.cpp` restaura también desde `atexit`, no solo al final de `main`.
2. **Y no cambia nada salvo que se le pida a propósito** con
   `--cambiar-volumen`. Sin la bandera, mide y calla.

La lección no es "acuérdate de restaurar". Es que **una herramienta que toca
estado global de la máquina no puede depender de llegar viva hasta su última
línea**, y que si además el estado es del usuario, tocarlo tiene que ser una
decisión explícita y no un efecto colateral de medir. Por eso existe ahora
`vol.cpp`: la herramienta contraria, que enseña y arregla volúmenes.

## 1. La onda cambiaba de forma al trasladarse

Síntoma: mientras se graba y la gráfica se desplaza, las curvas no solo se
mueven — cambian de forma.

Causa, en `ssbwave.c`, `i_envelope`: los picos se agrupaban **por columna de
píxel**. La columna de un pico depende de dónde esté el borde de la ventana, y
ese borde se mueve en cada fotograma. Un pico junto a una frontera salta de
columna, cambia el mínimo y el máximo de las dos, y el contorno entero tiembla —
aunque el audio de debajo lleve minutos congelado.

La corrección: agrupar por una rejilla de **tiempo absoluto**, no de píxeles. Un
pico cae siempre en la misma celda pase lo que pase con la ventana; lo único que
cambia al desplazarse es dónde se dibuja esa celda.

### Medido

`probes/run-estable.ps1`: se graba hasta desbordar el buffer (para que esté
trasladándose de verdad), se toman dos capturas separadas 1.5 s y se compara el
contorno alineándolo por el desplazamiento.

```
columnas con onda: A=963  B=962
sin alinear, diferencia media de altura: 17.89 px
alineado: desplazamiento 121 px -> diferencia media 1.22 px (sobre 853 columnas)
```

1.22 px sobre una banda de ~100 px de alto es el ruido de que la traslación no
cae en un número entero de píxeles. La onda se traslada y ya.

### Y las capturas dejaron de mentir

Este probe usa **PrintWindow**, no `CopyFromScreen`. PrintWindow le pide a la
ventana que se dibuje, así que otra aplicación encima no puede falsear la
medida. En `docs/12` una captura acabó fotografiando una conversación ajena que
se había puesto delante; con PrintWindow eso no puede pasar.

## 2. El medidor que no medía

Había un medidor de nivel por pista, pero mostraba `peak_level`: **el pico desde
que arrancó**, que nunca baja. En cuanto suena algo fuerte una vez se queda
lleno y deja de informar — justo cuando más falta hace, que es cuando una fuente
ha dejado de entregar audio y uno intenta averiguar por qué graba silencio.

Ahora el motor lleva un nivel instantáneo con caída exponencial (~150 ms), y el
medidor muestra:

- barra en **escala de decibelios** sobre 60 dB (lineal en amplitud, una voz
  normal a −25 dBFS mueve la barra un 5 % y todo parece muerto),
- **marca del pico** histórico, para no perderlo de vista,
- el valor numérico en dB, o **«sin señal» en rojo** cuando no llega nada.

Eso último es la respuesta directa a «¿por qué sale vacío?»: ahora se ve.

## 3. Lo de WhatsApp: qué se sabe y qué no

Medido con `probes/nivel.cpp`, que captura varias fuentes **a la vez** y las
compara en la misma ventana de tiempo:

```
fuente                      paquetes   SILENT   rms dBFS  pico dBFS   veredicto
app:WhatsApp                     603        0    -999.00    -999.00   datos a CERO
app:msedgewebview2               603        0    -999.00    -999.00   datos a CERO
output                           600        0    -109.71     -86.73   practicamente silencio
```

Lo que **sí** se sabe:

- El loopback por proceso **funciona**: 603 paquetes, ninguno marcado SILENT, al
  ritmo correcto. No es que no se abra ni que no entregue.
- El árbol de procesos es correcto: `WhatsApp.Root.exe (6936) → msedgewebview2
  (2172) → msedgewebview2 (4608)`, y se captura con
  `PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE`, que cubre descendientes.
- Su sesión de audio está al 100 % y sin silenciar.

Lo que **no** se sabe: si con WhatsApp sonando de verdad el audio aparece. En
todas mis mediciones `output` estaba en −109 dBFS, o sea que no sonaba nada — y
una fuente en silencio no distingue "no captura" de "no había nada que
capturar". **No se puede concluir nada de una medición así**, y por eso no se
concluye.

La prueba decisiva la tiene que hacer quien pueda hacer sonar WhatsApp:

```
nivel 8 app:WhatsApp app:msedgewebview2 output
```

Si `output` dice HAY AUDIO y las otras dos dicen CERO, el audio de WhatsApp no
sale por ese árbol de procesos y hay que capturarlo por el dispositivo. Si
alguna de las de app dice HAY AUDIO, ya sabemos cuál usar.

## 4. La barra que se cortaba

La ventana medía 1280 px, pero la pantalla escala al 125 %: el área útil son
~1024 px lógicos. La barra pedía ~1100 y el último botón salía cortado.

Se quitó **«Quitar última»**. No por espacio: porque cada banda ya tiene su
**X**, que cierra la pista que señalas en vez de la última. Un botón que hace
algo peor que otro que ya está ahí solo gasta sitio y obliga a cortar los demás.
Con eso y estrechar el desplegable de fuentes (340 → 250) todo entra.

La cabecera de cada pista dice ahora también la resolución (`48000 Hz 24 bit`)
y, para las aplicaciones, **por qué salida están sonando**.

## 5. Qué le falta, en orden de lo que más se echaría en falta

**Lo que impide usarlo en serio hoy:**

1. **Linux.** `ssb_core/src/linux/` no existe. Estaba en el alcance desde
   `docs/01` y sigue sin empezar.
2. **Recuperar el buffer tras un cierre inesperado.** Los segmentos están en
   disco, pero al arrancar no se leen: un cuelgue se lleva horas de grabación
   que técnicamente siguen ahí.
3. **Aviso de disco lleno.** El techo por pista son 2 GB, pero nadie mira si el
   disco tiene sitio. Con varias pistas largas se llena y no hay aviso.

**Lo que se echa en falta a diario:**

4. **Atajo global.** Guardar lo que acaba de pasar exige tener la ventana
   delante. Un atajo de sistema es la mitad del valor de un programa así.
5. **Marcadores.** Poner una marca mientras ocurre algo, para volver luego.
6. **Recortar la selección con precisión** (arrastrar sus bordes, ajustar a
   silencio) en vez de tener que acertar el arrastre.
7. **Sesión persistente.** Las pistas y sus fuentes no se recuerdan; hay que
   montarlas cada vez. Los ajustes sí se recuerdan desde `docs/11`.

**Calidad y formatos:**

8. **FLAC.** Hoy la exportación comprimida es solo con pérdida (MP3/AAC). Se
   guarda sin pérdida internamente y se exporta con pérdida, que es raro.
9. **Remuestreo al exportar**, para juntar fuentes que no van a la misma
   frecuencia. Ahora la mezcla exige que coincidan.
10. **Normalizar o limitar** al exportar, en vez de solo bajar la ganancia
    cuando la suma se pasa.

**Robustez, con deuda ya identificada:**

11. **Reanudar cuando el dispositivo cambia o desaparece** (los auriculares
    Bluetooth se apagan). Hoy la pista se queda muda para siempre.
12. **Acotar el mutex durante la exportación.** La auditoría de `docs/12` lo
    señaló con citas: exportar retiene el mutex de la pista mientras lee,
    descomprime y escribe. No causó ningún hueco medido, pero es el bloqueo más
    largo del programa y está en el camino del hilo de captura.
13. **Usar `devPosition` para medir los huecos**, en vez del reloj. Sigue
    pendiente por lo que dice `docs/13`: la medición da un desfase constante de
    −320 frames que no sé explicar, y no se construye sobre un número que no se
    entiende.
14. **`Saved 0 of 1 tracks`**, visto una vez en `docs/12` y nunca reproducido.
    Sigue sin explicación.
