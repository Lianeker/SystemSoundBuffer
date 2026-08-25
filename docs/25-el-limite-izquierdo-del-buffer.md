# 25 — El límite izquierdo del buffer

Con el buffer puesto en 5 minutos, lo máximo seleccionable eran 172 s, luego
257 s, y el borde izquierdo pegaba saltos hacia la derecha.

## La causa

`ssb_core/src/ssb_ring.c`. El índice del anillo se dimensiona **al crear** la
pista:

```c
r->cap = (seconds * rate + 2 * max_segment_frames) / SSB_BLOCK_FRAMES + 64;
```

y `ssb_ring_set_seconds` actualizaba `max_seconds`, el tamaño de segmento y
`keep`, pero **no `cap`**. Al agrandar el buffer, el índice se llenaba con la
capacidad vieja y `i_enforce_budget` entraba por aquí:

```c
if (r->count >= r->cap)
    force = 1;
```

`force` significa «libera aunque duela»: se salta la comprobación que impide
bajar de lo pedido. El buffer se quedaba clavado donde le permitiera el índice
viejo, y cada descarte tiraba un segmento entero —ya del tamaño nuevo, mayor—,
que es el salto del borde izquierdo.

## Reproducido

Pista creada con 120 s; a mitad de la grabación se piden 300 s:

```
  t= 160 s   pedido  120 s   cubre  127,26 s   ok
  t= 190 s   pedido  300 s   cubre  137,42 s   CORTO
  t= 220 s   pedido  300 s   cubre  140,32 s   CORTO
  ...
  t= 520 s   pedido  300 s   cubre  136,96 s   CORTO
```

Oscila entre 123 y 140 s y no pasa de ahí. El número sale de la cuenta:

```
cap = (120·48000 + 2·(120·48000/16)) / 4096 + 64 = 1646 bloques
1646 · 4096 / 48000 = 140,5 s
```

`probes/run-buffer-agrandar.ps1` es esa prueba.

## El arreglo

`i_grow_index`: reserva un índice nuevo del tamaño que pide la duración nueva,
copia los bloques en orden lógico y sustituye. Solo crece; encoger no hace falta
y obligaría a decidir qué se tira.

Después:

```
  t= 190 s   pedido  300 s   cubre  153,34 s   CORTO
  t= 220 s   pedido  300 s   cubre  184,23 s   CORTO
  t= 250 s   pedido  300 s   cubre  215,64 s   CORTO
  t= 280 s   pedido  300 s   cubre  246,44 s   CORTO
  t= 310 s   pedido  300 s   cubre  277,78 s   CORTO
  t= 340 s   pedido  300 s   cubre  308,41 s   ok
  ...
  t= 520 s   pedido  300 s   cubre  317,20 s   ok
```

Sube hasta lo pedido y se queda entre 305 y 318. Los `CORTO` de la subida son
correctos: no se pueden cubrir 300 s antes de que pasen 300 s.

El caso de siempre —buffer fijo desde el principio— no cambia:
`probes/run-buffer-limite.ps1` con 60 s da 62,04 / 61,01 / 63,74 / 62,29.

## Dos hipótesis que resultaron falsas

Vale la pena dejarlas escritas, porque las dos parecían razonables:

**1. «El techo de bytes.»** Si el anillo llenara sus 512 MB descartaría por
bytes, que también es un descarte forzado. Falso: la interfaz pide 2 GB
(`ssbapp.c:171`) y 300 s de dos pistas comprimidas son unos 30 MB.

**2. «El loopback deja de entregar cuando no suena nada.»** Es cierto para
algunas APIs y explicaría un buffer que no avanza. Falso aquí. Medido con el
sistema en silencio:

```
salida   (loopback)  40,02 s capturados en 40 s, 360 bloques en silencio
entrada  (microfono) 39,93 s capturados en 40 s
```

WASAPI entrega paquetes marcados como silencio y el motor los guarda. El tiempo
avanza igual.

## Un detalle de método

Las dos primeras pasadas de la prueba de agrandar dieron el mismo resultado que
antes del arreglo. El enlace había fallado —`LNK1168: no se puede abrir
ssbgui.exe para escritura`, con un ejecutable anterior todavía vivo— y la prueba
corrió el binario viejo. Un fallo de enlace en medio de la salida no se ve si
solo se mira el resultado de la prueba.

## Lo que sigue abierto de lo mismo que reportaste

- **`buffer N` antes de añadir una pista no hace nada.** Se aplica a las pistas
  que existen, y si no hay ninguna se pierde: la primera pista se crea con el
  valor guardado. Se ve en el registro: `buffer: 20 s en 0 pista(s)` y la pista
  siguiente nace con 120.
- **Reproducir se queda parado un rato antes de sonar.** Es el mismo camino que
  exportar, y va en el hilo de interfaz: escribe un WAV por pista y luego los
  suma. Con 5 minutos y dos pistas son unos 290 MB de entrada y salida antes de
  que empiece a sonar.
- **«Parar no para la cinta»**: no se ha podido reproducir. Tras `stop`, el
  tramo seleccionable se queda en 21,50 s y sigue igual 18 s después. Puede que
  lo que se movía fuera el borde izquierdo por el fallo de arriba, que sí seguía
  saltando mientras se grababa.
- **Congelar y volver a grabar salta 100 s**: sin reproducir todavía.
