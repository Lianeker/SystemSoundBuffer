# Tres defectos serios: exportacion corrupta y vista inestable

Fecha: 2026-08-24. Continua [`07-buffer-y-pistas.md`](07-buffer-y-pistas.md).

## 1. Carpetas de pista reutilizadas: la causa de los ficheros vacios

El directorio de cada pista se nombraba con su **indice**:

```c
bstd_sprintf(dir, sizeof(dir), "%s/pista%u", app->dir, app->ntracks);
```

Al cerrar una pista del medio, el array se desplaza y `ntracks` baja. La
siguiente pista que se anade recibe **la carpeta de una pista que sigue viva**.
Dos anillos escribiendo los mismos `seg-XXXXXXXX.dat` se pisan mutuamente: los
bloques del indice de uno apuntan a datos del otro, o a ficheros que el otro
acaba de truncar. Lo que se exporta sale corrupto o vacio.

Corregido con un contador que **no se reutiliza jamas** (`app->track_seq`), y de
paso el anillo borra sus segmentos al destruirse para no dejar huerfanos.

Verificado con `probes/run-carpetas.ps1`: se arranca con tres pistas
(`pista1, pista2, pista3`), se quita la ultima y se anade otra. Resultado:
`pista1, pista2, pista3, pista4`. Antes la nueva habria reutilizado `pista3`.

## 2. Al reducir el buffer, se quedaba muy por debajo de lo pedido

Sintoma: con la vista en 10 s, la grabacion aparecia apinada a la izquierda y
casi no se veia.

El descarte va por **segmentos enteros**, y el tamano de segmento se fija al
crear el anillo (1/16 del buffer). Un buffer de 5 min tiene segmentos de 18.75 s.
Al reducir a 10 s, `i_enforce_budget` descartaba segmentos de 18.75 s hasta bajar
del limite, y se pasaba de largo: pedias 10 s y te quedaban cuatro, o menos. La
ventana seguia siendo de 10 s, asi que lo poco que quedaba ocupaba una franja a
la izquierda.

Corregido distinguiendo **por que** se descarta:

- por bytes o por indice lleno hay que liberar si o si;
- **por duracion, solo si lo que queda sigue cubriendo lo pedido.**

El invariante que ya estaba escrito —*el buffer nunca se queda corto*— ahora se
respeta tambien despues de un cambio de tamano.

**Mi propia prueba lo estaba delatando y no lo vi**: decia
`tras reducir a 5 s, cubre 4.52 s` y pasaba, porque solo comprobaba el limite
superior (`span <= 5.0 * 1.30`). Ahora comprueba los dos:

```
(tras reducir a 5 s, cubre 5.72 s)
  el buffer se encoge de verdad                  ok
  y NUNCA se queda por debajo de lo pedido       ok
```

Una asercion con un solo lado es media asercion.

## 3. La vista, con una sola regla

Habia dos modos: anclada al principio mientras el buffer se llenaba, anclada al
ahora en regimen. El cambio de uno a otro es, por definicion, un salto. Y como el
corte dependia de si el circular ya habia descartado algo, ocurria en un momento
distinto en cada pista.

Ahora hay **una regla y solo una**:

```
ventana = [ahora - buffer, ahora]
```

El ancho del lienzo es el buffer, siempre. El borde derecho es el instante mas
reciente. Mientras se llena, lo que falta se ve como vacio a la izquierda; una
vez lleno, la onda ocupa todo el ancho y se traslada. Una regla sola no puede
saltar.

Ademas, `wave_resize` ya no llama a `view_content_size` si el tamano no ha
cambiado: esa llamada puede mover las barras de desplazamiento, y eso vuelve a
disparar `OnSize`.

## Una correccion sobre el metodo

Al empezar a investigar dije que habia **reproducido** los ficheros vacios: mi
script grababa, paraba, seleccionaba todo, guardaba, y no aparecia ningun
fichero. Era falso. La captura de esa ejecucion mostraba un navegador ocupando la
ventana: el foco se lo habia llevado otra aplicacion y `Ctrl+A`/`Ctrl+S` fueron a
parar alli.

El fallo estaba en el instrumento, no en el programa — otra vez, como en docs/04
con el script que no era consciente del DPI. La leccion se repite porque me la
salte otra vez: **antes de creer una reproduccion, comprobar que el escenario es
el que crees**. La captura estaba ahi desde el primer momento y bastaba mirarla.

Los dos defectos reales aparecieron despues, leyendo el codigo con la pregunta
correcta: *que tiene que pasar para que salga un fichero vacio?* La respuesta
—que el anillo no encuentre sus segmentos— llevo directamente a la colision de
carpetas.
