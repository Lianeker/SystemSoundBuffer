# 32 — El informe que nadie miraba

Al salir, el SDK cuenta los recursos que se quedaron sin soltar:

```
Non-dealloc Mutex: 3/2
Non-dealloc Threads: 1/0
```

Ese informe ha encontrado los dos únicos defectos de memoria de este proyecto, y
las dos veces por casualidad: apareció en la salida de una prueba que se estaba
leyendo por otra cosa. Esta ronda cierra el segundo y convierte el informe en una
puerta.

## El mutex

`ssbapp.c:1687` crea el cerrojo del trabajo de exportación con `bmutex_create`, y
en todo SSB **no había un solo `bmutex_close`**. Es la misma familia que el hilo
de `docs/28`: cuando la exportación se mudó a un hilo propio, el nacimiento se
escribió y la muerte no.

```c
if ((*app)->job.th != NULL)
{
    bthread_wait((*app)->job.th);
    bthread_close(&(*app)->job.th);
}
/* Despues de esperar al hilo, nunca antes: es el cerrojo con el que ese
   hilo se comunica con la interfaz. */
if ((*app)->job.mtx != NULL)
    bmutex_close(&(*app)->job.mtx);
```

El orden importa y no es cosmético: cerrar el cerrojo antes de esperar al hilo
sería quitárselo de debajo mientras lo usa.

Éste no era grave —un mutex al salir del proceso no hace daño— pero el del hilo
sí lo era, y los dos venían del mismo descuido. El que avisa de los dos es el
mismo informe.

## La puerta

`probes/linux-interfaz.sh` guarda ahora la salida de la interfaz y falla si
aparece `Non-dealloc`. Dos líneas de guion para que un aviso que ya se emitía
deje de depender de que alguien lo lea.

Validada en los dos sentidos, comentando el `bmutex_close` y volviéndolo a poner:

```
### con la fuga ###
  salida: 1
  [14:57:50] Non-dealloc Mutex: 3/2
  FALLO: la interfaz dejo recursos sin soltar al salir

### sin la fuga ###
  salida: 0
  interfaz en Linux (pulse): OK
```

De paso, el mensaje de fallo mentía: cualquier problema salía como «el WAV
exportado no lleva el tono», también cuando el WAV estaba perfecto. Ahora el tono
y los recursos se cuentan por separado y cada uno dice lo suyo.

## Estado

Las tres pruebas de Linux en verde, en los dos servidores donde aplica, y sin
línea `Non-dealloc` en el informe de salida:

```
Total bytes a/dellocated: 4204088, 4204088
```

Con esto se cierra la lista que abrió `docs/28`. Todo lo que la tabla del README
promete sobre Linux lo respalda una ejecución en la puerta.
