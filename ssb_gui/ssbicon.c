/* Iconos de transporte dibujados por codigo.
 *
 * ¿Por que no un simbolo de texto (▶ ⏹ ⏺) en el boton? Porque el color no se
 * puede tocar: NAppGUI expone `button_font` pero no color de texto ni de fondo,
 * porque debajo hay un control nativo del sistema. Un circulo de grabacion que
 * no es rojo no es un circulo de grabacion.
 *
 * Lo que si acepta un boton nativo es una IMAGEN (`button_image`). Asi que los
 * simbolos se generan aqui como bitmaps RGBA y se los damos hechos. Sale mas
 * barato y mas portable que dibujar botones propios, y encima se ven igual en
 * los tres sistemas en vez de heredar las rarezas de cada uno.
 *
 * Los bordes van suavizados a mano con supermuestreo: un triangulo de 16 px sin
 * antialiasing se ve como una escalera y canta mucho al lado de los controles
 * del sistema, que si lo tienen.
 */
#include "ssbgui.h"
#include <stdlib.h>

#define ICON_PX 18   /* lado del icono, en pixeles */
#define SS 4         /* supermuestreo: SSxSS por pixel */

/* Cobertura de un punto por la figura, evaluada en subpixeles. */
typedef int (*i_shape_fn)(double x, double y);

/* Todas las figuras se definen en un cuadrado [0,1]x[0,1]. */

static int i_shape_play(double x, double y)
{
    /* Triangulo apuntando a la derecha, con algo de margen. */
    double x0 = 0.24, x1 = 0.80;
    double t;
    if (x < x0 || x > x1)
        return 0;
    t = (x - x0) / (x1 - x0);      /* 0 en la base, 1 en la punta */
    return (y >= 0.5 - 0.42 * (1.0 - t) && y <= 0.5 + 0.42 * (1.0 - t)) ? 1 : 0;
}

static int i_shape_stop(double x, double y)
{
    return (x >= 0.24 && x <= 0.76 && y >= 0.24 && y <= 0.76) ? 1 : 0;
}

static int i_shape_rec(double x, double y)
{
    double dx = x - 0.5, dy = y - 0.5;
    return (dx * dx + dy * dy <= 0.28 * 0.28) ? 1 : 0;
}

static int i_shape_pause(double x, double y)
{
    if (y < 0.24 || y > 0.76)
        return 0;
    return ((x >= 0.26 && x <= 0.43) || (x >= 0.57 && x <= 0.74)) ? 1 : 0;
}

static Image *i_make(i_shape_fn shape, color_t col)
{
    byte_t *px = (byte_t *)heap_malloc(ICON_PX * ICON_PX * 4, "ssbicon");
    Image *img;
    uint32_t ix, iy;
    uint8_t r = 0, g = 0, b = 0, a = 0;
    color_get_rgba(col, &r, &g, &b, &a);

    for (iy = 0; iy < ICON_PX; ++iy)
    {
        for (ix = 0; ix < ICON_PX; ++ix)
        {
            int sx, sy, hits = 0;
            byte_t *p = px + ((size_t)iy * ICON_PX + ix) * 4;
            for (sy = 0; sy < SS; ++sy)
            {
                for (sx = 0; sx < SS; ++sx)
                {
                    double fx = ((double)ix + ((double)sx + 0.5) / SS) / (double)ICON_PX;
                    double fy = ((double)iy + ((double)sy + 0.5) / SS) / (double)ICON_PX;
                    hits += shape(fx, fy);
                }
            }
            /* Color plano, transparencia proporcional a la cobertura: asi el
               icono se funde con el fondo del boton sea cual sea, claro u
               oscuro, sin tener que saber cual es. */
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = (byte_t)(255 * hits / (SS * SS));
        }
    }
    img = image_from_pixels(ICON_PX, ICON_PX, ekRGBA32, px, NULL, 0);
    heap_free((byte_t **)&px, ICON_PX * ICON_PX * 4, "ssbicon");
    return img;
}

void icons_create(App *app)
{
    /* Los colores son los de siempre en cualquier grabadora: rojo para grabar,
       verde para reproducir, neutro para parar y pausar. No es decoracion: es
       lo que permite reconocer el boton sin leerlo. */
    app->ico_rec = i_make(i_shape_rec, color_rgb(220, 60, 60));
    app->ico_stop = i_make(i_shape_stop, color_rgb(190, 190, 195));
    app->ico_play = i_make(i_shape_play, color_rgb(60, 190, 100));
    app->ico_pause = i_make(i_shape_pause, color_rgb(230, 200, 90));
}

void icons_destroy(App *app)
{
    if (app->ico_rec != NULL)
        image_destroy(&app->ico_rec);
    if (app->ico_stop != NULL)
        image_destroy(&app->ico_stop);
    if (app->ico_play != NULL)
        image_destroy(&app->ico_play);
    if (app->ico_pause != NULL)
        image_destroy(&app->ico_pause);
}
