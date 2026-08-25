# 27 — Lo escrito y lo que ocupa

La barra de estado decía `disco N MB` y ese número no paraba de subir, con el
buffer fijo en unos segundos. La pregunta era razonable: ¿de verdad el circular
está pisando lo viejo, o va llenando el disco sin parar?

El circular pisa. La cifra estaba mal.

## La medición

`probes/run-disco-crece.ps1` graba con un buffer corto —para forzar descartes de
verdad— y cada segundo mide el tamaño real de los segmentos en la carpeta, desde
fuera del proceso. 150 s de grabación con un buffer de 10 s:

```
carpeta:  oscila entre 200 y 350 KB durante toda la grabación, sin tendencia
motor:    disco 4183.5 KB
bloques:  120 vivos, 1638 descartados
```

Los 120 bloques vivos son los 10 s pedidos, y no se mueven de ahí. La carpeta
oscila porque se descarta por segmentos enteros y porque los bloques comprimidos
no ocupan lo mismo según lo que suene, no porque crezca con el tiempo.

Los 4183 KB eran los 1758 bloques escritos desde el arranque, contando los 1638
que ya no existen.

## La causa

`ssb_track` llevaba un contador `disk_bytes` que se sumaba en cada bloque
codificado y no se restaba nunca:

```c
if (ssb_ring_append(...) == ssb_ok)
    t->disk_bytes += n;
```

Ese contador hacía dos trabajos a la vez. Para el ratio de compresión es el
correcto: `raw_bytes / disk_bytes` sobre todo lo que ha pasado por el codificador.
Para «cuánto ocupa esto» es el equivocado, y era el que se enseñaba en la barra
de estado, en la etiqueta de cada pista, en `tracks` y en la salida del CLI.

El dato bueno ya existía: `ssb_ring_bytes` devuelve `total_bytes`, que **sí** se
resta al descartar (`ssb_ring.c:215`) y que el propio selftest ya comprobaba
contra el techo de bytes. Nunca se conectó con las estadísticas de la pista.

## El arreglo

Separar los dos números en `ssb_track_stats`:

| | |
|---|---|
| `written_bytes` | comprimido escrito, acumulado. Con `raw_bytes` da el ratio |
| `disk_bytes` | lo que ocupan **ahora** los segmentos vivos: `ssb_ring_bytes` |

El campo interno pasa a llamarse `enc_bytes`, que es lo que siempre fue. Los
cuatro sitios que enseñaban la cifra no cambian: leen `disk_bytes` y ahora
reciben la ocupación. El CLI enseña las dos, que en el detalle final interesan:

```
disco 220.7 KB ahora (escritos 1105.3 KB de 7504.0 KB crudos  ->  ratio 6.79:1)
```

Comprobado en la interfaz, 135 s grabando con un buffer de 10 s:

```
t= 15 s   0.3 MB
t= 55 s   0.3 MB
t= 95 s   0.1 MB
t=135 s   0.3 MB
```

Antes subía en cada toma. La magnitud del desvío la da la medición del CLI: a
los 150 s con el mismo buffer de 10 s, 4183 KB escritos contra 258 KB ocupados
—dieciséis veces— y la separación sigue creciendo mientras la grabación dure.

## Dónde vive el audio

De paso, porque la pregunta venía junto a la otra: el buffer en vivo **está en
disco**, no en RAM. Cada pista tiene su carpeta bajo `ssb-gui-buffer/` con
segmentos `seg-XXXXXXXX.dat` de tamaño fijo; en RAM solo está el índice de
bloques (instante, frames, bytes, segmento, desplazamiento) más el bloque de
4096 frames que se está acumulando. Descartar es borrar un fichero: se eligió
así para no tener que hacer aritmética de solape con registros de tamaño
variable, que es donde se cuelan los fallos. Está en la cabecera de
`ssb_ring.c`.

O sea que `disco` era el nombre correcto; lo que engañaba era el número.

## La guarda

En el selftest, junto a la comprobación del techo de bytes:

```c
i_check(ssb_ring_bytes(r) < (uint64_t)blocks_written * (uint64_t)n,
        "los bytes vivos no son los bytes escritos");
```

Es la distinción que se había perdido, escrita como aserción.

## Estado

`ctest` verde. `disco` estable en las cuatro tomas de 135 s, y la carpeta medida
desde fuera coincide con la cifra que enseña el programa.
