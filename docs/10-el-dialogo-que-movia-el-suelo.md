# 10 — El diálogo que movía el suelo

Síntoma reportado: «sigue roto el export de selección, y mp3 directamente
exporta un wav, todo vacío en ambos casos».

Dos síntomas, una sola causa, y ninguno de los dos apuntaba hacia ella.

## Lo que se veía

Con varias pistas grabadas, pulsar «Save selection...», elegir carpeta y
aceptar producía ficheros `.wav` con cabecera correcta y cero muestras. Si el
formato elegido era MP3, el fichero que aparecía era un `.wav` — también vacío.

La segunda parte tiene explicación inmediata en cuanto se conoce la primera:
`ssb_encode` escribe primero el WAV y luego lo recodifica. Si el WAV sale vacío,
la recodificación falla al leerlo y el código deja el `.wav` en su sitio. No
había ningún fallo en el camino de MP3; era un daño colateral honesto.

## Lo que pasaba

`comwin_save_file` acaba en `oscomwin_file()` de NAppGUI, que construye el
`OPENFILENAME` así (`nappgui_src/src/osgui/win/oscomwin.c:217`):

```c
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
```

Falta `OFN_NOCHANGEDIR`. Sin esa bandera, el diálogo de fichero de Windows
**cambia el directorio de trabajo del proceso** a la carpeta que el usuario
haya visitado. Está documentado; simplemente nadie lo espera al abrir un
selector de ficheros.

Nuestros anillos guardaban sus segmentos en rutas relativas
(`ssb-gui-buffer/pistaN/seg-*.dat`). En el instante en que el usuario abría el
diálogo y navegaba a otra carpeta, esas rutas pasaban a apuntar a un sitio
donde no había nada. `ssb_ring_read` devolvía `ssb_err_io`, el WAV se creaba con
su cabecera y sin una sola muestra, y el usuario veía ficheros vacíos.

La ironía es que el fallo se disparaba **precisamente al guardar**, es decir en
la única operación durante la cual el usuario está mirando. Y era intermitente
en apariencia: si aceptabas el diálogo sin moverte de carpeta, no pasaba nada.

## La corrección

Tres piezas, de la más importante a la menos:

1. **Rutas absolutas desde el arranque.** `ssb_abs_path()` en
   `ssb_core/src/ssb_util.c` (`GetFullPathNameA` en Windows, `getcwd` + prefijo
   en POSIX). `app->dir` y `app->savedir` se resuelven a absolutas en `i_create`,
   antes de que nada pueda mover el suelo. Esta sola pieza arregla el fallo.
2. **Restaurar el directorio de trabajo** con `ssb_set_cwd()` justo después de
   `comwin_save_file` y de `comwin_select_dir`. Nuestras rutas ya son absolutas,
   pero deja el proceso como estaba en vez de confiar en que nadie más use una
   relativa. El comando `folder <ruta>` también absolutiza su argumento.
3. **`CoUninitialize` equilibrado** en `ssb_core/src/win/ssb_encode_win.cpp`. No
   causaba este fallo, pero salió a la luz mirándolo: si el hilo ya estaba en un
   apartamento distinto, `CoInitializeEx` devolvía `RPC_E_CHANGED_MODE` sin
   inicializar nada, y el `CoUninitialize` incondicional del final descontaba una
   referencia que no era suya. Ahora solo se llama si la inicialización funcionó.

Las tres capas son deliberadas: la (1) resuelve el problema, la (2) impide que
vuelva por otra puerta, la (3) es deuda encontrada de camino.

## Lo que se midió

`probes/run-dialogo.ps1` recorre exactamente el camino que fallaba: graba 12 s
con dos pistas, selecciona todo, pulsa «Save selection...» y **escribe en el
diálogo una ruta absoluta a otra carpeta**, que es lo que provoca el cambio de
directorio.

```
prueba-1.mp3   320111 B  192 kbps 48000 Hz joint   555 frames = 13.32 s  OK
prueba-2.mp3   320111 B  192 kbps 48000 Hz joint   555 frames = 13.32 s  OK

prueba-1.wav   48000 Hz 2ch 13.28 s  pico  3329  rms   424.6  56% con señal
prueba-2.wav   48000 Hz 2ch 13.28 s  pico 11670  rms  1785.2  71% con señal
```

Comprobado que llevan audio y no silencio (RMS y porcentaje de muestras con
señal), que las dos pistas traen contenido distinto (81.8 % de los bytes del MP3
difieren entre ellas), que la frecuencia de muestreo es la del origen —48 kHz, no
el resampleo silencioso a 32 kHz de `MFCreateMP3MediaSink`— y que las duraciones
coinciden entre pistas.

## La lección, otra vez

La primera versión de esta prueba manejaba la aplicación a base de pulsaciones
simuladas. Devolvió dos MP3 válidos... de 0.55 s. Los ficheros eran correctos;
la selección que se exportó no era la que yo creía haber hecho, porque parte de
las pulsaciones se perdieron por el camino. Sin mirar el HUD del programa habría
dado por bueno un resultado que no probaba nada.

Es la tercera vez en este proyecto que el instrumento de medida miente antes que
el código medido (la primera fue la captura de pantalla sin conciencia de DPI; la
segunda, un navegador que robó el foco). La prueba definitiva monta el escenario
por el **modo comandos**, que es determinista, y reserva el ratón para lo único
que no se puede hacer de otra forma: el diálogo del sistema.

Antes de acusar a nadie, valida el instrumento.

## Aguas arriba

Esto es un defecto del SDK, no nuestro. Queda anotado en el repo de NAppGUI como
`backlog/NAP-043-el-dialogo-de-fichero-cambia-el-directorio-de-trabajo.md`, junto
con dos hallazgos de la misma línea: falta `OFN_OVERWRITEPROMPT` (guardar machaca
un fichero existente sin preguntar) y `OFN_FILEMUSTEXIST` se aplica también al
camino de guardar, donde no pinta nada.
