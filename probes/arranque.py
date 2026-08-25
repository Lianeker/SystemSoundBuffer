"""Cuanto blanco queda en cada toma del arranque.

    python probes/arranque.py build-dbg/arranque

Las tomas se llaman tNNNN.png con los milisegundos desde que se lanzo el
proceso. Para cada una se mide que porcentaje de pixeles es casi blanco. Con la
interfaz ya dibujada en oscuro ese porcentaje es practicamente cero; mientras los
controles no han pintado, es casi todo.
"""
import os
import re
import sys

try:
    from PIL import Image
except ImportError:
    print("hace falta Pillow: python -m pip install pillow")
    sys.exit(2)

UMBRAL = 200  # a partir de aqui se cuenta como blanco


def main(carpeta):
    tomas = []
    for f in sorted(os.listdir(carpeta)):
        m = re.match(r"t(\d+)\.png$", f)
        if m is None:
            continue
        tomas.append((int(m.group(1)), os.path.join(carpeta, f)))

    if not tomas:
        print("no hay tomas en %s" % carpeta)
        return 1

    primera_limpia = None
    for (ms, path) in tomas:
        im = Image.open(path).convert("RGB")
        w, h = im.size
        px = im.load()
        blancos = 0
        total = 0
        # Se muestrea en rejilla: contar todos los pixeles no cambia la
        # conclusion y multiplica el tiempo por cien.
        for y in range(0, h, 4):
            for x in range(0, w, 4):
                r, g, b = px[x, y]
                if r > UMBRAL and g > UMBRAL and b > UMBRAL:
                    blancos += 1
                total += 1
        pct = 100.0 * blancos / total if total else 0.0
        marca = ""
        if pct < 2.0 and primera_limpia is None:
            primera_limpia = ms
            marca = "  <- ya no queda blanco"
        print("  %5d ms   blanco %5.1f %%%s" % (ms, pct, marca))

    print("")
    if primera_limpia is None:
        print("el blanco no llego a irse en las tomas hechas")
        return 1
    print("blanco visible durante %d ms desde el lanzamiento" % primera_limpia)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "arranque"))
