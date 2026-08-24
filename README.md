# SystemSoundBuffer

Continuous audio recorder with a ring buffer and trimming.

Records several sources at once, each on its own track: the full system output,
specific applications (WhatsApp, a browser, a game) and input devices. It all
goes into a ring buffer of configurable duration. You pause, select a stretch of
the timeline, save it, and the recording is not interrupted.

![SystemSoundBuffer](docs/img/systemsoundbuffer.png)

Two tracks recording at once, nine seconds of the timeline selected.

## Status

**Works on Windows, engine and interface.** It records several sources at once,
compresses losslessly, keeps the ring buffer on disk, draws the waveforms live
and dumps any selected stretch to WAV, aligned across tracks.

```
ssbgui                                      # the interface (this is what you open)
ssbgui --src output --src app:WhatsApp      # starts with tracks set
ssbgui --theme dark --lang es --export mp3  # theme, language and output format
ssbgui --mode cmd                           # command mode (Ctrl+K)
ssbgui --mode small                         # reduced mode (Ctrl+M)

ssb list                                    # the engine on the command line (console)
ssb rec --secs 20 --buffer 300 --src output --src app:WhatsApp --src input
ssb drift --secs 25 --src output --src app:WhatsApp --csv drift.csv
ssb encode grabacion.wav grabacion.mp3 192
ssb selftest
```

Outstanding: capture on Linux. See [`docs/`](docs/).

## Building

The interface needs the **NAppGUI** SDK installed. This fork is the one used,
which carries fixes the interface takes for granted —among them real dark mode
on Windows and the flat button with text—:
<https://github.com/Lianeker/nappgui-modernize> (branch `modernize`).

It is expected at `../nappgui`, next to this directory:

```
Prog/
  nappgui/               <- the SDK fork, with its install/
  SystemSoundBuffer/     <- this repository
```

```powershell
. ..\nappgui\tools\env.ps1

# Engine and tests, in any configuration:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build

# The interface, against the installed SDK:
cmake -S . -B build-dbg -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-dbg
.\build-dbg\ssbgui.exe
```

If the SDK is missing, CMake skips the interface with a message and builds the
rest. To install it: `cd ..\nappgui` and `.\tools\verify.ps1` (installs Debug).
For Release, `cmake -S nappgui_src -B build-rel -G Ninja
-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=install` and `cmake --install
build-rel`.

Everything builds with `/W4 /WX`: zero warnings, and warnings are errors.

## Standalone executable

`build\ssbgui.exe` built in Release **needs nothing else**: no Visual C++
redistributable and no DLL beyond the ones Windows ships. It is ~600 KB and
copies anywhere.

```
> dumpbin /DEPENDENTS ssbgui.exe
  dwmapi ole32 MMDevAPI AVRT MF MFPlat MFReadWrite COMCTL32 UxTheme
  WS2_32 gdiplus SHLWAPI KERNEL32 USER32 GDI32 SHELL32 COMDLG32 ADVAPI32
```

Two SDK decisions are what allow this: it builds with the **static** CRT (`/MT`)
and links the **static** WebView2 loader. Without that you would need the
redistributable and `WebView2Loader.dll` alongside.

One practical warning: the ring buffer is created in `ssb-gui-buffer`, **next to
the working directory it is launched from**. It is best to keep it in a folder of
its own and not on the desktop.

## Intended layout

```
SystemSoundBuffer/
  ssb_core/    engine: sources, ring buffers, compression, peak map, saving. C, no GUI.
    win/       WASAPI capture          (verified)
    linux/     PulseAudio/PipeWire capture (pending)
                 (macOS out of scope: see docs/01, section 6)
  ssb_gui/     interface with NAppGUI, consumer of ssb_core
    ssbapp.c   window, controls, life cycle
    ssbwave.c  the waveform canvas: drawing, selection, zoom
  probes/      test programs that hold up the design decisions
  docs/        decisions, with the evidence that backs them
```

## Decisions

Each document carries the measurements that back its decisions and the gaps that
remain open. They are read before writing code.

The notes themselves are in Spanish; the descriptions below are not.

- [`01-viabilidad-y-decisiones.md`](docs/01-viabilidad-y-decisiones.md) — what
  can be captured, with which API, and the nine architecture decisions.
- [`02-motor.md`](docs/02-motor.md) — the engine, the real compression ratios and
  the ring buffer defect.
- [`03-sincronizacion.md`](docs/03-sincronizacion.md) — why the timeline is set
  by the clock and not by the frame count.
- [`04-interfaz.md`](docs/04-interfaz.md) — the interface and what was verified
  of it by automating mouse and keyboard.
- [`05-ajustes.md`](docs/05-ajustes.md) — why the dropouts cannot be solved, the
  defect that made the waveform dance, and the window theme.
- [`06-exportacion.md`](docs/06-exportacion.md) — MP3 and AAC with the system
  encoder, language, track muting and the silent-output warning.
- [`07-buffer-y-pistas.md`](docs/07-buffer-y-pistas.md) — the end of the
  recording that was being lost, changing the buffer on the fly, and per-track
  buttons.
- [`08-corrupcion-y-vista.md`](docs/08-corrupcion-y-vista.md) — reused track
  folders, a discard that went too far, and the view with a single ruler.
- [`09-modos.md`](docs/09-modos.md) — the view asks the engine, reduced mode and
  command mode.
- [`10-el-dialogo-que-movia-el-suelo.md`](docs/10-el-dialogo-que-movia-el-suelo.md)
  — the Win32 file dialog changes the working directory.
- [`11-chasquidos-y-ajustes.md`](docs/11-chasquidos-y-ajustes.md) — where the
  artefacts came from and which settings survive shutdown.
- [`12-el-reloj-que-temblaba.md`](docs/12-el-reloj-que-temblaba.md) — placing by
  time or counting frames, and why you have to validate the measuring
  instrument.
- [`13-calidad-mezcla-y-escucha.md`](docs/13-calidad-mezcla-y-escucha.md) — 24
  bits, mixing into one file and playback of the selection.
- [`14-onda-estable-y-que-falta.md`](docs/14-onda-estable-y-que-falta.md) — the
  waveform that changed when scrolling, and the list of what is missing.
- [`15-la-interseccion-y-los-botones.md`](docs/15-la-interseccion-y-los-botones.md)
  — a track added late and the per-track buttons.
- [`16-la-union-y-el-peso-de-un-boton.md`](docs/16-la-union-y-el-peso-de-un-boton.md)
  — fix the problem where it is, not where it shows.
- [`17-el-congelamiento-y-la-cabeza-que-saltaba.md`](docs/17-el-congelamiento-y-la-cabeza-que-saltaba.md)
  — COM on the interface thread, and the device watcher.
- [`18-el-tema-oscuro-que-nunca-existio.md`](docs/18-el-tema-oscuro-que-nunca-existio.md)
  — `gui_dark_mode()` was lying on Windows, and the keyboard commands.
- [`19-la-barra-al-estilo-del-explorador.md`](docs/19-la-barra-al-estilo-del-explorador.md)
  — flat buttons, separators, and how you photograph a highlight.
- [`20-un-microfono-es-mono.md`](docs/20-un-microfono-es-mono.md) — the mixer
  refused to join mono with stereo, and what was breaking underneath.

## Dependencies

None external. The GUI uses the NAppGUI SDK from `../nappgui`, consumed with
`find_package(nappgui)` from its install prefix. Capture uses only operating
system APIs.