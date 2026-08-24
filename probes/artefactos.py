"""Cuenta interrupciones en los WAV exportados.

Busca la firma de un empalme mal hecho: un tramo de silencio EXACTO (las dos
muestras a cero) metido en medio de audio que suena, y con la senal del mismo
signo a los dos lados — es decir, un trozo que alguien escribio a ceros dentro
de una onda que iba por su sitio. Eso es lo que se oye como chasquido.

    python probes/artefactos.py build-dbg/art

Dos umbrales, y los dos importan:

MIN_FRAMES
    Un cero suelto NO es un artefacto: es la onda cruzando el eje. En audio
    tranquilo los dos canales pueden cruzar en la misma muestra y dar un cero
    perfecto de un solo frame. Con el umbral a 1 frame, este detector marco una
    "interrupcion" de 0.02 ms en una senal de -40 dB y me hizo creer que habia
    una regresion; el motor decia `huecos 0` y tenia razon. 16 frames son 0.33
    ms a 48 kHz: mil veces mas corto que cualquier hueco real (el mas pequeno
    que hemos medido son 12.7 ms) y a la vez imposible de confundir con un cruce
    por cero.

MIN_LEVEL
    A niveles bajos el ruido de fondo cruza el cero constantemente. Se exige que
    la senal a los dos lados suene de verdad.

MAX_FRAMES
    Por encima de esto ya no es un artefacto sino un hueco de captura, que el
    motor cuenta aparte y del que informa. Aqui interesa lo que el motor NO vio.
"""
import os
import sys
import wave

MIN_FRAMES = 16
MAX_FRAMES = 2000
MIN_LEVEL = 300


def i_leer(path):
    w = wave.open(path)
    n = w.getnframes()
    ch = w.getnchannels()
    sr = w.getframerate()
    ancho = w.getsampwidth()
    crudo = w.readframes(n)
    w.close()
    if ancho != 2:
        return None, ch, sr
    muestras = []
    for i in range(0, len(crudo), 2):
        v = crudo[i] | (crudo[i + 1] << 8)
        muestras.append(v - 65536 if v >= 32768 else v)
    return muestras, ch, sr


def i_contar(path):
    m, ch, sr = i_leer(path)
    if m is None:
        return None, 0.0, []
    n = len(m) // ch
    hallazgos = []
    i = 0
    while i < n:
        if any(m[i * ch + c] != 0 for c in range(ch)):
            i += 1
            continue
        j = i
        while j < n and all(m[j * ch + c] == 0 for c in range(ch)):
            j += 1
        largo = j - i
        if MIN_FRAMES <= largo <= MAX_FRAMES and i > 0 and j < n:
            antes = m[(i - 1) * ch]
            despues = m[j * ch]
            if antes * despues > 0 and abs(antes) > MIN_LEVEL and abs(despues) > MIN_LEVEL:
                hallazgos.append((i / float(sr), largo * 1000.0 / sr))
        i = j
    return hallazgos, n / float(sr), []


def main(carpeta):
    total = 0
    segundos = 0.0
    for f in sorted(os.listdir(carpeta)):
        if not f.lower().endswith('.wav'):
            continue
        hallazgos, dur, _ = i_contar(os.path.join(carpeta, f))
        if hallazgos is None:
            print('  %s: no es PCM de 16 bits, se salta' % f)
            continue
        segundos += dur
        total += len(hallazgos)
        for (t, ms) in hallazgos:
            print('  %s  en %.3f s  %.2f ms de silencio exacto' % (f, t, ms))
    print('interrupciones: %d en %.0f s' % (total, segundos))
    return 0 if total == 0 else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'art'))
