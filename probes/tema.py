# Mide si una zona de una captura esta en oscuro o en claro.
#
#     python3 probes/tema.py captura.ppm  x y ancho alto  [oscuro|claro]
#
# Lee PPM binario (P6) a mano: es cuatro lineas de cabecera y bytes RGB, y asi
# no hace falta ningun decodificador de imagenes. La captura se convierte a PPM
# con ImageMagick, que solo se necesita para la prueba y no para el programa.
#
# Sale 0 si la zona esta como se esperaba.
import sys

CLARO_MIN = 0.60   # luminancia por encima de esto es tema claro
OSCURO_MAX = 0.35  # y por debajo de esto, oscuro


def lee_ppm(path):
    d = open(path, 'rb').read()
    if d[:2] != b'P6':
        sys.exit('no es un PPM binario (P6)')
    campos = []
    i = 2
    while len(campos) < 3:
        while i < len(d) and d[i:i + 1].isspace():
            i += 1
        if d[i:i + 1] == b'#':
            while i < len(d) and d[i:i + 1] != b'\n':
                i += 1
            continue
        j = i
        while j < len(d) and not d[j:j + 1].isspace():
            j += 1
        campos.append(int(d[i:j]))
        i = j
    i += 1
    w, h, mx = campos
    # ImageMagick escribe 16 bits por canal segun la version, asi que hay que
    # admitir los dos anchos en vez de dar por hecho el de esta maquina.
    if mx == 255:
        return w, h, d[i:i + w * h * 3], 1
    if mx == 65535:
        return w, h, d[i:i + w * h * 6], 2
    sys.exit('PPM con maxval %d, no admitido' % mx)


def main():
    if len(sys.argv) < 6:
        sys.exit('uso: tema.py captura.ppm x y ancho alto [oscuro|claro]')
    path = sys.argv[1]
    x0, y0, aw, ah = (int(v) for v in sys.argv[2:6])
    espera = sys.argv[6] if len(sys.argv) > 6 else ''

    w, h, px, ancho = lee_ppm(path)
    if x0 + aw > w or y0 + ah > h:
        sys.exit('la zona %d,%d %dx%d no cabe en la imagen %dx%d' % (x0, y0, aw, ah, w, h))

    paso = 3 * ancho
    tope = float(255 if ancho == 1 else 65535)
    sr = sg = sb = 0
    n = 0
    for y in range(y0, y0 + ah):
        base = (y * w + x0) * paso
        for k in range(aw):
            o = base + k * paso
            if ancho == 1:
                sr += px[o]
                sg += px[o + 1]
                sb += px[o + 2]
            else:
                sr += (px[o] << 8) | px[o + 1]
                sg += (px[o + 2] << 8) | px[o + 3]
                sb += (px[o + 4] << 8) | px[o + 5]
            n += 1
    r, g, b = sr / n / tope, sg / n / tope, sb / n / tope
    lum = 0.21 * r + 0.72 * g + 0.07 * b

    print('  zona %d,%d %dx%d  ->  rgb(%d, %d, %d)  luminancia %.3f'
          % (x0, y0, aw, ah, r * 255, g * 255, b * 255, lum))

    if espera == 'oscuro':
        if lum > OSCURO_MAX:
            print('  FALLO: se esperaba oscuro y la luminancia es %.3f (limite %.2f)'
                  % (lum, OSCURO_MAX))
            return 1
        print('  OK: oscuro')
    elif espera == 'claro':
        if lum < CLARO_MIN:
            print('  FALLO: se esperaba claro y la luminancia es %.3f (limite %.2f)'
                  % (lum, CLARO_MIN))
            return 1
        print('  OK: claro')
    return 0


if __name__ == '__main__':
    sys.exit(main())
