# Comprueba que un WAV contiene un tono de la frecuencia que se le dice.
#
#     python3 probes/tono.py grabacion.wav 440 [segundos_esperados]
#
# Sin dependencias: Goertzel escrito a mano sobre el modulo `wave` de la
# biblioteca estandar. Tiene que poder correr en un runner limpio, y anadir
# numpy solo para esto seria una dependencia nueva por la puerta de atras.
#
# Sale 0 si el tono esta y domina; distinto de 0 si no. Es una puerta, no un
# informe: comparar contra frecuencias senuelo es lo que distingue "grabo el
# tono" de "grabo algo", y eso ultimo lo cumple hasta el ruido.
import math
import struct
import sys
import wave

# Multiplos NO enteros de la frecuencia buscada: un armonico de la senal
# real puntuaria alto y ensuciaria la comparacion.
SENUELOS = (0.41, 0.63, 1.37, 2.29, 3.61)
DOMINIO_MIN = 20.0   # veces por encima del mejor senuelo
RMS_MIN = 0.005      # por debajo de esto es silencio, no senal


def leer_canal0(path):
    w = wave.open(path, 'rb')
    try:
        canales = w.getnchannels()
        ancho = w.getsampwidth()
        rate = w.getframerate()
        frames = w.getnframes()
        crudo = w.readframes(frames)
    finally:
        w.close()

    if ancho == 2:
        vals = struct.unpack('<%dh' % (len(crudo) // 2), crudo)
        escala = 32768.0
    elif ancho == 3:
        vals = []
        for i in range(0, len(crudo) - 2, 3):
            v = crudo[i] | (crudo[i + 1] << 8) | (crudo[i + 2] << 16)
            if v & 0x800000:
                v -= 1 << 24
            vals.append(v)
        escala = 8388608.0
    else:
        sys.exit('ancho de muestra no soportado: %d bytes' % ancho)

    canal0 = [vals[i] / escala for i in range(0, len(vals) - canales + 1, canales)]
    return canal0, rate, frames, canales, ancho * 8


def goertzel(x, rate, freq):
    """Energia de x en `freq`, normalizada por la longitud."""
    k = 2.0 * math.cos(2.0 * math.pi * freq / rate)
    s1 = 0.0
    s2 = 0.0
    for v in x:
        s0 = v + k * s1 - s2
        s2 = s1
        s1 = s0
    n = float(len(x))
    return (s1 * s1 + s2 * s2 - k * s1 * s2) / (n * n)


def main():
    if len(sys.argv) < 3:
        sys.exit('uso: tono.py fichero.wav frecuencia [segundos_esperados]')
    path = sys.argv[1]
    freq = float(sys.argv[2])
    esperados = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0

    canal0, rate, frames, canales, bits = leer_canal0(path)
    dur = frames / float(rate) if rate else 0.0
    print('  %s' % path)
    print('  %d Hz, %d canal(es), %d bits, %.3f s' % (rate, canales, bits, dur))

    if len(canal0) < rate:
        sys.exit('  FALLO: menos de un segundo de audio (%d muestras)' % len(canal0))

    # Dos segundos del centro: evita el arranque y el corte del final, y con
    # 2 s la resolucion es de 0.5 Hz, de sobra para separar los senuelos.
    ancho = min(2 * rate, len(canal0))
    ini = (len(canal0) - ancho) // 2
    x = canal0[ini:ini + ancho]

    rms = math.sqrt(sum(v * v for v in x) / float(len(x)))
    obj = goertzel(x, rate, freq)
    print('  RMS %.5f' % rms)
    print('  %8.1f Hz  %.3e   <- buscado' % (freq, obj))

    peor = 0.0
    for m in SENUELOS:
        f = freq * m
        if f >= rate / 2.0:
            continue
        e = goertzel(x, rate, f)
        print('  %8.1f Hz  %.3e' % (f, e))
        if e > peor:
            peor = e

    fallos = []
    if rms < RMS_MIN:
        fallos.append('silencio: RMS %.5f por debajo de %.5f' % (rms, RMS_MIN))
    dominio = obj / peor if peor > 0.0 else float('inf')
    if dominio < DOMINIO_MIN:
        fallos.append('el tono no domina: %.1fx sobre el mejor senuelo, hace falta %.0fx'
                      % (dominio, DOMINIO_MIN))
    if esperados > 0.0 and abs(dur - esperados) > max(1.0, esperados * 0.25):
        fallos.append('duracion %.3f s, se esperaban ~%.1f s' % (dur, esperados))

    print('  dominio %.0fx' % dominio)
    if fallos:
        for f in fallos:
            print('  FALLO: %s' % f)
        return 1
    print('  OK: el tono de %.0f Hz esta en la grabacion' % freq)
    return 0


if __name__ == '__main__':
    sys.exit(main())
