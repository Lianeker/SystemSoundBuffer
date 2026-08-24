# SystemSoundBuffer

Grabadora continua de audio con buffer circular y recorte.

Graba varias fuentes a la vez, cada una en su pista: la salida completa del
sistema, aplicaciones concretas (WhatsApp, un navegador, un juego) y dispositivos
de entrada. Todo va a un buffer circular de duración configurable. Se pausa, se
selecciona un tramo de la línea de tiempo, se guarda, y la grabación no se corta.

## Estado

**Funciona en Windows, motor e interfaz.** Graba varias fuentes a la vez,
comprime sin pérdida, mantiene el buffer circular en disco, dibuja las ondas en
vivo y vuelca a WAV cualquier tramo seleccionado, alineado entre pistas.

```
ssbgui                                      # la interfaz (esto es lo que abres)
ssbgui --src output --src app:WhatsApp      # arranca con pistas puestas
ssbgui --theme dark --lang es --export mp3  # tema, idioma y formato de salida
ssbgui --mode cmd                           # modo comandos (Ctrl+K)
ssbgui --mode small                         # modo reducido (Ctrl+M)

ssb list                                    # el motor por linea de comandos (consola)
ssb rec --secs 20 --buffer 300 --src output --src app:WhatsApp --src input
ssb drift --secs 25 --src output --src app:WhatsApp --csv drift.csv
ssb encode grabacion.wav grabacion.mp3 192
ssb selftest
```

Pendiente: la captura en Linux. Ver [`docs/`](docs/).

## Compilar

La interfaz necesita el SDK **NAppGUI** instalado. Se usa esta bifurcación, que
lleva correcciones que la interfaz da por hechas —entre ellas el modo oscuro real
en Windows y el botón plano con texto—:
<https://github.com/Lianeker/nappgui-modernize> (rama `modernize`).

Se espera en `../nappgui`, junto a este directorio:

```
Prog/
  nappgui/               <- la bifurcacion del SDK, con su install/
  SystemSoundBuffer/     <- este repositorio
```

```powershell
. ..\nappgui\tools\env.ps1

# Motor y pruebas, en cualquier configuracion:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build

# La interfaz, contra el SDK instalado:
cmake -S . -B build-dbg -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-dbg
.\build-dbg\ssbgui.exe
```

Si falta el SDK, el CMake omite la interfaz con un mensaje y compila el resto.
Instalarlo: `cd ..\nappgui` y `.\tools\verify.ps1` (instala Debug). Para
Release, `cmake -S nappgui_src -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release
-DCMAKE_INSTALL_PREFIX=install` y `cmake --install build-rel`.

Todo compila con `/W4 /WX`: cero avisos, y los avisos son errores.

## Ejecutable suelto

`build\ssbgui.exe` compilado en Release **no necesita nada más**: ni el
redistribuible de Visual C++ ni ninguna DLL fuera de las que trae Windows. Son
~600 KB y se copian a donde sea.

```
> dumpbin /DEPENDENTS ssbgui.exe
  dwmapi ole32 MMDevAPI AVRT MF MFPlat MFReadWrite COMCTL32 UxTheme
  WS2_32 gdiplus SHLWAPI KERNEL32 USER32 GDI32 SHELL32 COMDLG32 ADVAPI32
```

Son dos decisiones del SDK las que lo permiten: se compila con el CRT **estático**
(`/MT`) y enlaza el cargador **estático** de WebView2. Sin eso harían falta el
redistribuible y `WebView2Loader.dll` al lado.

Un aviso práctico: el buffer circular se crea en `ssb-gui-buffer`, **junto al
directorio de trabajo desde el que se lanza**. Conviene dejarlo en una carpeta
propia y no en el escritorio.

## Disposición prevista

```
SystemSoundBuffer/
  ssb_core/    motor: fuentes, ring buffers, compresion, mapa de picos, guardado. C, sin GUI.
    win/       captura WASAPI          (verificada)
    linux/     captura PulseAudio/PipeWire (pendiente)
                 (macOS fuera de alcance: ver docs/01, seccion 6)
  ssb_gui/     interfaz con NAppGUI, consumidora de ssb_core
    ssbapp.c   ventana, controles, ciclo de vida
    ssbwave.c  el lienzo de ondas: dibujo, seleccion, zoom
  probes/      programas de prueba que sostienen las decisiones de diseño
  docs/        decisiones, con la evidencia que las respalda
```

## Decisiones

Cada documento lleva las mediciones que respaldan sus decisiones y los huecos que
quedan abiertos. Se leen antes de escribir código.

- [`01-viabilidad-y-decisiones.md`](docs/01-viabilidad-y-decisiones.md) — qué se
  puede capturar, con qué API, y las nueve decisiones de arquitectura.
- [`02-motor.md`](docs/02-motor.md) — el motor, los ratios de compresión reales y
  el defecto del buffer circular.
- [`03-sincronizacion.md`](docs/03-sincronizacion.md) — por qué la línea de
  tiempo la marca el reloj y no la cuenta de frames.
- [`04-interfaz.md`](docs/04-interfaz.md) — la interfaz y lo que se verificó de
  ella automatizando ratón y teclado.
- [`05-ajustes.md`](docs/05-ajustes.md) — por qué las pérdidas no se pueden
  resolver, el defecto que hacía bailar la onda, y el tema de la ventana.
- [`06-exportacion.md`](docs/06-exportacion.md) — MP3 y AAC con el codificador
  del sistema, idioma, silenciado de pistas y el aviso de salida muda.
- [`07-buffer-y-pistas.md`](docs/07-buffer-y-pistas.md) — el final de grabación
  que se perdía, cambio de buffer en caliente, y botones por pista.
- [`08-corrupcion-y-vista.md`](docs/08-corrupcion-y-vista.md) — carpetas de pista
  reutilizadas, descarte que se pasaba de largo, y la vista con una sola regla.
- [`09-modos.md`](docs/09-modos.md) — la vista pregunta al motor, modo reducido y
  modo comandos.
- [`10-el-dialogo-que-movia-el-suelo.md`](docs/10-el-dialogo-que-movia-el-suelo.md)
  — el diálogo de fichero de Win32 cambia el directorio de trabajo.
- [`11-chasquidos-y-ajustes.md`](docs/11-chasquidos-y-ajustes.md) — de dónde
  salían los artefactos y qué ajustes sobreviven al cierre.
- [`12-el-reloj-que-temblaba.md`](docs/12-el-reloj-que-temblaba.md) — colocar por
  tiempo o contar frames, y por qué hay que validar el instrumento de medida.
- [`13-calidad-mezcla-y-escucha.md`](docs/13-calidad-mezcla-y-escucha.md) — 24
  bits, mezcla en un fichero y reproducción de lo seleccionado.
- [`14-onda-estable-y-que-falta.md`](docs/14-onda-estable-y-que-falta.md) — la
  onda que cambiaba al desplazarse, y la lista de lo que falta.
- [`15-la-interseccion-y-los-botones.md`](docs/15-la-interseccion-y-los-botones.md)
  — una pista añadida tarde y los botones por pista.
- [`16-la-union-y-el-peso-de-un-boton.md`](docs/16-la-union-y-el-peso-de-un-boton.md)
  — arreglar el problema donde está, no donde se ve.
- [`17-el-congelamiento-y-la-cabeza-que-saltaba.md`](docs/17-el-congelamiento-y-la-cabeza-que-saltaba.md)
  — COM en el hilo de interfaz, y el vigilante de dispositivos.
- [`18-el-tema-oscuro-que-nunca-existio.md`](docs/18-el-tema-oscuro-que-nunca-existio.md)
  — `gui_dark_mode()` mentía en Windows, y los comandos de teclado.
- [`19-la-barra-al-estilo-del-explorador.md`](docs/19-la-barra-al-estilo-del-explorador.md)
  — botones planos, separadores, y cómo se fotografía un resaltado.
- [`20-un-microfono-es-mono.md`](docs/20-un-microfono-es-mono.md) — el mezclador
  se negaba a juntar mono con estéreo, y qué se rompía por debajo.

## Dependencias

Ninguna externa. La GUI usa el SDK NAppGUI de
`../nappgui`, consumido con `find_package(nappgui)` desde su prefijo de
instalación. La captura usa solo APIs del sistema operativo.
