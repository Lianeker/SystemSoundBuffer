/* La vista de ondas: una banda por pista, seleccion por arrastre, zoom con la
 * rueda y desplazamiento con el boton derecho.
 *
 * Dibuja SIEMPRE del mapa de picos (`ssb_track_peaks`), nunca del audio: por eso
 * la vista es instantanea aunque el buffer tenga horas y este comprimido.
 *
 * Modelo de la ventana de tiempo: el ancho del lienzo ES el tamano del buffer.
 * Mientras el buffer se llena, la onda crece de izquierda a derecha y lo que
 * queda por llenar se ve como tal. Cuando esta lleno, el borde derecho se queda
 * en el ahora y toda la onda se traslada hacia atras. No se puede seleccionar
 * ni mirar nada que este fuera del buffer: lo que se ve es lo que hay.
 */
#include "ssbgui.h"
#include <math.h>

#define RULER_H 22.0f
#define HUD_H 40.0f
#define TOP_H (RULER_H + HUD_H)
#define LANE_GAP 4.0f
#define LANE_H 175.0f
#define LANE_H_SMALL 46.0f
#define CMD_LINE_H 14.0f

/* Altura de banda segun el modo. El modo reducido dibuja lo minimo para grabar:
   la onda, el nivel y poco mas. */
static real32_t i_lane_h(const App *app)
{
    return (app->compact == TRUE) ? LANE_H_SMALL : LANE_H;
}

/* Alto de la franja superior: en modo reducido no hay regla de tiempo. */
static real32_t i_top_h(const App *app)
{
    real32_t h = (app->compact == TRUE) ? 20.0f : TOP_H;
    if (app->cmdmode == TRUE)
        h += (real32_t)CMD_LINES * CMD_LINE_H + 6.0f;
    return h;
}
/* Botones de cada banda, dibujados por nosotros: los controles nativos no se
   pueden meter dentro de un View, y ademas tienen que desplazarse con la lista. */
#define BTN_W 22.0f
#define BTN_H 16.0f
#define BTN_Y 3.0f

/* ------------------------------------------------------------------ paleta */

/* Elige color segun el tema. Por omision manda el del sistema, que es lo que
   hace gui_alt_color(); con --theme se puede forzar uno de los dos. */
static color_t i_alt(const App *app, const color_t light, const color_t dark)
{
    if (app->theme == 1)
        return light;
    if (app->theme == 2)
        return dark;
    return gui_alt_color(light, dark);
}

void wave_palette(App *app)
{
    Palette *p = &app->pal;
    p->back = i_alt(app, color_rgb(246, 247, 249), color_rgb(18, 20, 23));
    p->lane_a = i_alt(app, color_rgb(255, 255, 255), color_rgb(20, 22, 26));
    p->lane_b = i_alt(app, color_rgb(240, 242, 246), color_rgb(23, 25, 29));
    p->axis = i_alt(app, color_rgb(206, 210, 217), color_rgb(45, 48, 54));
    p->empty = i_alt(app, color_rgb(230, 232, 236), color_rgb(13, 14, 17));
    p->ruler_bg = i_alt(app, color_rgb(232, 234, 238), color_rgb(28, 30, 34));
    p->ruler_line = i_alt(app, color_rgb(188, 192, 200), color_rgb(70, 74, 82));
    p->ruler_text = i_alt(app, color_rgb(96, 102, 112), color_rgb(150, 155, 165));
    p->hud_bg = i_alt(app, color_rgb(238, 240, 244), color_rgb(24, 26, 30));
    p->hud_text = i_alt(app, color_rgb(55, 60, 68), color_rgb(175, 182, 196));
    p->hud_dim = i_alt(app, color_rgb(128, 134, 144), color_rgb(120, 126, 138));
    p->sel_fill = i_alt(app, color_rgba(40, 110, 220, 45), color_rgba(90, 160, 255, 60));
    p->sel_edge = i_alt(app, color_rgb(40, 110, 220), color_rgb(120, 180, 255));
    p->warn = i_alt(app, color_rgb(180, 110, 10), color_rgb(255, 190, 90));
    p->ok = i_alt(app, color_rgb(25, 125, 55), color_rgb(140, 220, 150));
    p->meter_bg = i_alt(app, color_rgb(214, 218, 224), color_rgb(40, 44, 50));
    /* La barra va con `gui_alt_color` y NO con `i_alt`: los tres colores de aqui
       abajo conviven con controles NATIVOS (los desplegables, los botones), y
       esos siguen al tema de Windows pase lo que pase. Con `--theme light` en un
       Windows oscuro, hacerles caso pintaba etiquetas claras sobre una barra que
       el sistema seguia dibujando oscura. `--theme` manda en el lienzo, que lo
       dibujamos nosotros enteros; en la barra manda el sistema.

       El oscuro es EXACTAMENTE el que pinta NAppGUI detras de un boton plano
       (32,32,32). Si no coincide al pixel, cada boton se dibuja como un
       rectangulo ligeramente distinto sobre la barra y se ven todos, que es
       justo lo contrario de lo que se busca. */
    p->chrome = gui_alt_color(color_rgb(240, 240, 240), color_rgb(32, 32, 32));
    p->chrome_tx = gui_alt_color(color_rgb(30, 30, 30), color_rgb(220, 224, 230));
    /* La raya entre grupos: se tiene que ver y no se tiene que mirar. */
    p->sep = gui_alt_color(color_rgb(205, 205, 205), color_rgb(62, 62, 62));
}

color_t wave_track_color(const App *app, uint32_t i)
{
    switch (i % 6)
    {
    case 0:
        return i_alt(app, color_rgb(20, 115, 185), color_rgb(90, 200, 255));
    case 1:
        return i_alt(app, color_rgb(196, 105, 15), color_rgb(255, 170, 90));
    case 2:
        return i_alt(app, color_rgb(35, 135, 60), color_rgb(150, 230, 130));
    case 3:
        return i_alt(app, color_rgb(155, 55, 145), color_rgb(230, 130, 220));
    case 4:
        return i_alt(app, color_rgb(150, 120, 15), color_rgb(255, 230, 120));
    default:
        return i_alt(app, color_rgb(85, 85, 175), color_rgb(180, 180, 255));
    }
}

/* --------------------------------------------------------- tiempo <-> pixel */

static ssb_time i_win_from(const App *app)
{
    ssb_time span = (ssb_time)(app->span_secs * (double)SSB_TICKS_PER_SEC);
    return (app->win_to > span) ? app->win_to - span : 0;
}

void wave_time_to_x(const App *app, ssb_time t, real32_t width, real32_t *x)
{
    double span = app->span_secs * (double)SSB_TICKS_PER_SEC;
    ssb_time from = i_win_from(app);
    double d;
    cassert_no_null(x);
    /* `ssb_time` no tiene signo: restar hacia atras desborda. Un instante
       anterior al borde izquierdo debe dar una x negativa, no 1.8e19. */
    d = (t >= from) ? (double)(t - from) : -(double)(from - t);
    *x = (real32_t)(d / span * (double)width);
}

ssb_time wave_x_to_time(const App *app, real32_t x, real32_t width)
{
    double span = app->span_secs * (double)SSB_TICKS_PER_SEC;
    ssb_time from = i_win_from(app);
    double t;
    if (width <= 0)
        return from;
    t = (double)from + (double)x / (double)width * span;
    if (t < 0)
        t = 0;
    return (ssb_time)t;
}

/* Un boton de banda, dibujado por nosotros.
 *
 * Estos NO son controles del sistema, y no pueden serlo: un control nativo no
 * se puede meter dentro de un View ni desplazarse con la lista al hacer scroll.
 * Son portables sin mas —solo usan draw2d, que NAppGUI implementa en Win32,
 * Cocoa y GTK— y encima se ven igual en los tres en vez de heredar las rarezas
 * de cada uno.
 *
 * El texto se centra MIDIENDOLO (`draw_text_extents`), no con un desplazamiento
 * a ojo: con un ancho fijo la letra queda descentrada en cuanto cambia la fuente
 * del sistema o el escalado de la pantalla, y se nota. */
static void i_draw_btn(DCtx *ctx, const App *app, real32_t x, real32_t y,
                       const char_t *label, int active, int hover, color_t on_color)
{
    const Palette *pal = &app->pal;
    real32_t tw = 0, th = 0;
    color_t bg = active ? on_color : pal->meter_bg;

    if (hover == 1 && active == 0)
        bg = pal->hud_dim;   /* respuesta al raton: sin ella no parece pulsable */

    /* Rectangulos, sin redondear. Un boton de 22x16 con esquinas redondeadas
       gasta la mitad del borde en curvas y se lee peor; a este tamano lo simple
       gana. */
    draw_fill_color(ctx, bg);
    draw_rect(ctx, ekFILL, x, y, BTN_W, BTN_H);
    draw_line_color(ctx, hover ? pal->sel_edge : pal->axis);
    draw_rect(ctx, ekSTROKE, x, y, BTN_W, BTN_H);

    draw_font(ctx, app->font);
    draw_text_extents(ctx, label, -1.0f, &tw, &th);
    draw_text_color(ctx, active ? pal->lane_a : pal->hud_text);
    draw_text(ctx, label, x + (BTN_W - tw) * 0.5f, y + (BTN_H - th) * 0.5f);
}

/* Rectangulo del boton `n` (0 = silenciar, 1 = cerrar) de la banda `i`. */
static void i_lane_btn(const App *app, uint32_t i, uint32_t n, real32_t *x, real32_t *y)
{
    *x = 6.0f + (real32_t)n * (BTN_W + 4.0f);
    *y = i_top_h(app) + (real32_t)i * i_lane_h(app) + BTN_Y;
}

/* Recorta un instante a lo que de verdad hay en el buffer. Es lo que impide
   seleccionar aire y llevarse la sorpresa al guardar. */
static ssb_time i_clamp_to_buffer(App *app, ssb_time t)
{
    ssb_time from = 0, to = 0;
    if (app_span(app, &from, &to) == FALSE)
        return t;
    if (t < from)
        return from;
    if (t > to)
        return to;
    return t;
}

/* Escala del medidor: lineal en decibelios sobre 60 dB.
   Con escala lineal en amplitud, una voz normal (-25 dBFS) mueve la barra un
   5% y todo parece apagado; en decibelios ocupa mas de la mitad, que es lo que
   uno espera de un medidor. */
static double i_meter_scale(double amp)
{
    double db;
    if (amp <= 0.000001)
        return 0.0;
    db = 20.0 * log10(amp);
    if (db <= -60.0)
        return 0.0;
    if (db >= 0.0)
        return 1.0;
    return (db + 60.0) / 60.0;
}

/* ------------------------------------------------------------------ dibujo */

/* Envolvente de una pista, colocada en SU franja de tiempo.
 *
 * Ojo con esto: `ssb_track_peaks` devuelve solo los picos que existen, que no
 * tienen por que cubrir toda la ventana visible. Repartirlos entre todas las
 * columnas estira la onda sobre hueco vacio y miente sobre cuando paso cada
 * cosa. Hay que pedir la interseccion y dibujarla entre sus dos pixeles.
 */
/* Envolvente de una pista, colocando CADA pico en su instante.
 *
 * Antes se repartian los picos devueltos entre todas las columnas por indice.
 * Eso daba por hecho que los picos cubrian exactamente la ventana pedida, y no
 * es asi: el anillo de picos y el de audio no cubren lo mismo, ni el tiempo
 * avanza igual que la cuenta de picos si la fuente pierde frames. El sintoma
 * era que la parte vieja de la onda se veia bailar en cada repintado.
 */
static uint32_t i_envelope(App *app, GTrack *gt, ssb_time from, ssb_time to,
                           real32_t w, real32_t ymid, real32_t half,
                           V2Df *pts, uint32_t maxpts)
{
    uint32_t got, k, n = 0;
    uint64_t cell = 0;
    int have = 0;
    int32_t mn = 0, mx = 0;
    ssb_time bucket;

    got = ssb_track_peaks(gt->track, from, to, app->peaks, app->peaks_cap);
    if (got == 0 || w < 1.0f)
        return 0;

    /* Los picos se agrupan por una rejilla de TIEMPO ABSOLUTO, no por columna
       de pixel.
     *
     * Agrupando por pixel, el grupo de cada pico depende de donde este el borde
     * de la ventana, y ese borde se mueve cada fotograma mientras se graba. Un
     * pico que cae junto a una frontera salta de columna, cambia el min/max de
     * las dos, y el contorno entero tiembla: la onda "cambia de forma" mientras
     * se traslada, aunque el audio de debajo lleve rato congelado.
     *
     * Con la rejilla anclada al reloj, un pico cae SIEMPRE en la misma celda
     * pase lo que pase con la ventana. Lo unico que cambia al desplazarse es
     * donde se dibuja la celda. La onda se traslada y ya. */
    bucket = (ssb_time)(app->span_secs * (double)SSB_TICKS_PER_SEC / (double)w);
    if (bucket == 0)
        bucket = 1;

    for (k = 0; k <= got; ++k)
    {
        uint64_t c = 0;
        if (k < got)
            c = (uint64_t)(app->peaks[k].t / bucket);

        if (k >= got || (have == 1 && c != cell))
        {
            /* Cierra la celda anterior antes de empezar la siguiente. */
            if (have == 1 && n + 2 <= maxpts)
            {
                real32_t x = 0;
                wave_time_to_x(app, (ssb_time)(cell * bucket), w, &x);
                if (x < 0)
                    x = 0;
                if (x > w - 1)
                    x = w - 1;
                pts[n].x = x;
                pts[n].y = ymid - (real32_t)mx / 32768.0f * half;
                n++;
                pts[n].x = x;
                pts[n].y = ymid - (real32_t)mn / 32768.0f * half - 1.0f;
                n++;
            }
            if (k >= got)
                break;
            have = 0;
        }
        if (have == 0)
        {
            cell = c;
            mn = app->peaks[k].min;
            mx = app->peaks[k].max;
            have = 1;
        }
        else
        {
            if (app->peaks[k].min < mn)
                mn = app->peaks[k].min;
            if (app->peaks[k].max > mx)
                mx = app->peaks[k].max;
        }
    }
    return n;
}

/* La regla se rotula respecto del borde vivo del buffer, no del borde de la
   ventana: "-10s" significa "hace diez segundos", siempre. */
static void i_draw_ruler(DCtx *ctx, App *app, real32_t w, real32_t top, ssb_time live)
{
    const Palette *pal = &app->pal;
    ssb_time from = i_win_from(app);
    double span = app->span_secs;
    double step, t;
    char_t txt[32];

    draw_fill_color(ctx, pal->ruler_bg);
    draw_rect(ctx, ekFILL, 0, top, w, RULER_H);

    step = 1.0;
    while (span / step > 14.0)
        step *= (step < 5.0) ? 5.0 : 2.0;
    while (span / step < 4.0 && step > 0.02)
        step /= 2.0;

    draw_line_color(ctx, pal->ruler_line);
    draw_text_color(ctx, pal->ruler_text);
    draw_font(ctx, app->font);
    for (t = 0.0; t < span + step; t += step)
    {
        real32_t x;
        ssb_time tt = from + (ssb_time)(t * (double)SSB_TICKS_PER_SEC);
        wave_time_to_x(app, tt, w, &x);
        if (x > w)
            break;
        draw_line(ctx, x, top, x, top + RULER_H);
        if (tt <= live)
        {
            double ago = ssb_time_to_sec(live - tt);
            bstd_sprintf(txt, sizeof(txt), ago >= 1.0 ? "-%.0fs" : "-%.1fs", ago);
            draw_text(ctx, txt, x + 3, top + 3);
        }
    }
}

static void i_draw_hud(DCtx *ctx, App *app, real32_t w, real32_t top)
{
    const Palette *pal = &app->pal;
    char_t txt[260];

    draw_fill_color(ctx, pal->hud_bg);
    draw_rect(ctx, ekFILL, 0, top + RULER_H, w, HUD_H);
    draw_line_color(ctx, pal->axis);
    draw_line(ctx, 0, top + TOP_H, w, top + TOP_H);
    draw_font(ctx, app->font);

    draw_text_color(ctx, pal->hud_text);
    draw_text(ctx, app->status, 8, top + RULER_H + 3);

    if (app->has_sel == TRUE)
    {
        ssb_time a = (app->sel_a < app->sel_b) ? app->sel_a : app->sel_b;
        ssb_time b = (app->sel_a < app->sel_b) ? app->sel_b : app->sel_a;
        bstd_sprintf(txt, sizeof(txt), T(app, TXT_SELECTION),
                     ssb_time_to_sec(b - a),
                     ssb_time_to_sec(app->live > b ? app->live - b : 0));
        draw_text_color(ctx, pal->sel_edge);
    }
    else
    {
        str_copy_c(txt, sizeof(txt), T(app, TXT_HINT));
        draw_text_color(ctx, pal->hud_dim);
    }
    draw_text(ctx, txt, 8, top + RULER_H + 20);

    if (app->message[0] != 0)
    {
        draw_text_color(ctx, pal->ok);
        draw_text(ctx, app->message, w * 0.52f, top + RULER_H + 20);
    }
    if (app->recording == TRUE)
    {
        /* El punto rojo lo dibujamos nosotros: los botones nativos no se pueden
           colorear y este es el estado que hay que ver de un vistazo. */
        draw_fill_color(ctx, color_rgb(220, 50, 50));
        draw_circle(ctx, ekFILL, w * 0.52f + 6, top + RULER_H + 9, 6);
        draw_text_color(ctx, color_rgb(220, 50, 50));
        draw_text(ctx, T(app, TXT_BANNER_RECORDING), w * 0.52f + 18, top + RULER_H + 3);
    }
    else if (app->ntracks > 0)
    {
        draw_text_color(ctx, pal->warn);
        draw_text(ctx, T(app, app->live > 0 ? TXT_BANNER_STOPPED : TXT_BANNER_READY),
                  w * 0.52f, top + RULER_H + 3);
    }
    if (app->frozen == TRUE)
    {
        draw_text_color(ctx, pal->warn);
        draw_text(ctx, T(app, TXT_BANNER_FROZEN), w * 0.52f, top + RULER_H + 3);
    }
}

/* Consola del modo comandos: las ultimas lineas, ancladas bajo la franja
   superior y desplazandose con el viewport como el resto de la cabecera. */
/* Chuleta de atajos (F1), dibujada ENCIMA de todo.
 *
 * Unos atajos que no se pueden consultar no existen: o se memorizan el primer
 * dia o no se usan nunca. Va sobre la onda y no en un dialogo aparte para poder
 * mirarla mientras se trabaja, que es cuando hace falta. */
static void i_draw_keyhelp(DCtx *ctx, App *app, real32_t w, real32_t top)
{
    /* Solo los nombres de tecla van fijos: son los mismos en cualquier
       idioma. Las descripciones salen de la tabla de textos, para que la
       chuleta no se quede a medio traducir al cambiar de idioma. */
    static const char_t *i_KEYS[] = {
        "Ctrl+R",
        "Ctrl+L",
        "Ctrl+H",
        "Ctrl+A",
        "Ctrl+S",
        "Ctrl+J",
        "Ctrl+D",
        "Ctrl+E",
        "Ctrl+G",
        "Ctrl+M",
        "Ctrl+K",
        "F1",
    };
    const Palette *pal = &app->pal;
    const uint32_t n = sizeof(i_KEYS) / sizeof(i_KEYS[0]);
    real32_t bw = 330.0f, lh = 17.0f;
    real32_t bh = (real32_t)n * lh + 34.0f;
    real32_t x = w - bw - 16.0f;
    real32_t y = top + 16.0f;
    uint32_t i;

    if (app->keyhelp == FALSE)
        return;
    if (x < 8.0f)
        x = 8.0f;

    draw_fill_color(ctx, pal->hud_bg);
    draw_rect(ctx, ekFILL, x, y, bw, bh);
    draw_line_color(ctx, pal->sel_edge);
    draw_rect(ctx, ekSTROKE, x, y, bw, bh);

    draw_font(ctx, app->font);
    draw_text_color(ctx, pal->sel_edge);
    draw_text(ctx, T(app, TXT_KEYS_TITLE), x + 12, y + 8);
    for (i = 0; i < n; ++i)
    {
        real32_t ly = y + 28.0f + (real32_t)i * lh;
        draw_text_color(ctx, pal->hud_text);
        draw_text(ctx, i_KEYS[i], x + 12, ly);
        draw_text_color(ctx, pal->hud_dim);
        draw_text(ctx, T(app, (ssb_txt)(TXT_KEY_00 + i)), x + 86, ly);
    }
}

static void i_draw_console(DCtx *ctx, App *app, real32_t w, real32_t top)
{
    const Palette *pal = &app->pal;
    real32_t y;
    uint32_t i;

    if (app->cmdmode == FALSE)
        return;
    y = top + ((app->compact == TRUE) ? 20.0f : TOP_H);
    draw_fill_color(ctx, pal->lane_a);
    draw_rect(ctx, ekFILL, 0, y, w, (real32_t)CMD_LINES * CMD_LINE_H + 6.0f);
    draw_line_color(ctx, pal->axis);
    draw_line(ctx, 0, y, w, y);
    draw_font(ctx, app->font);
    for (i = 0; i < CMD_LINES; ++i)
    {
        if (app->cmdout[i][0] == 0)
            continue;
        draw_text_color(ctx, app->cmdout[i][0] == '>' ? pal->sel_edge : pal->hud_text);
        draw_text(ctx, app->cmdout[i], 8, y + 3 + (real32_t)i * CMD_LINE_H);
    }
}

/* Cuanto cuesta pintar el lienzo entero, en milisegundos.
   Se mide porque de ello depende una decision de diseno: si dibujar los
   controles nosotros en vez de usar los del sistema sale caro o barato. Sin el
   numero, la respuesta seria una opinion. */
static void i_OnDraw(App *app, Event *e)
{
    const EvDraw *p = event_params(e, EvDraw);
    uint64_t t_ini = ssb_now();
    const Palette *pal = &app->pal;
    DCtx *ctx = p->ctx;
    V2Df vpos;
    S2Df vsize;
    real32_t w, h, top, xlive;
    real32_t lane_h;
    ssb_time from, to, live;
    uint32_t i;
    static V2Df pts[8192];

    /* El viewport es la unica fuente fiable de que se ve de verdad: con la vista
       desplazable, EvDraw describe el lienzo entero, no la parte visible. */
    view_viewport(app->view, &vpos, &vsize);
    w = vsize.width;
    h = vsize.height;
    top = vpos.y;
    unref(p);

    lane_h = i_lane_h(app);
    draw_clear(ctx, pal->back);
    draw_fill_color(ctx, pal->back);
    draw_rect(ctx, ekFILL, 0, 0, w, i_top_h(app) + (real32_t)app->ntracks * lane_h + h);

    if (app->ntracks == 0)
    {
        /* Sin pistas tambien hay que pintar la consola: en modo comandos es la
           unica forma de ver la respuesta a `help` o a `list`. */
        draw_text_color(ctx, pal->hud_dim);
        draw_font(ctx, app->font);
        draw_text(ctx, T(app, TXT_NO_TRACKS), 16, top + i_top_h(app) + 8);
        i_draw_console(ctx, app, w, top);
        i_draw_keyhelp(ctx, app, w, top);
        return;
    }

    from = i_win_from(app);
    to = app->win_to;
    live = app->live;
    wave_time_to_x(app, live, w, &xlive);
    if (xlive > w)
        xlive = w;
    if (xlive < 0)
        xlive = 0;

    for (i = 0; i < app->ntracks; ++i)
    {
        GTrack *gt = &app->tracks[i];
        real32_t y0 = i_top_h(app) + (real32_t)i * lane_h;
        real32_t ymid = y0 + lane_h * 0.5f;
        real32_t half = (lane_h - LANE_GAP) * 0.5f;
        uint32_t n;
        ssb_track_stats st;
        char_t txt[192];

        draw_fill_color(ctx, (i & 1) ? pal->lane_b : pal->lane_a);
        draw_rect(ctx, ekFILL, 0, y0, w, lane_h - LANE_GAP);

        /* Lo que esta pista no cubre se ve como vacio, no como silencio. */
        {
            ssb_time tf = 0, tt = 0;
            real32_t xa = 0, xb = w;
            if (ssb_track_span(gt->track, &tf, &tt) == ssb_ok)
            {
                wave_time_to_x(app, tf, w, &xa);
                wave_time_to_x(app, tt, w, &xb);
            }
            if (xa < 0)
                xa = 0;
            if (xa > w)
                xa = w;
            if (xb > w)
                xb = w;
            if (xb < 0)
                xb = 0;
            draw_fill_color(ctx, pal->empty);
            if (xa > 0)
                draw_rect(ctx, ekFILL, 0, y0, xa, lane_h - LANE_GAP);
            if (xb < w)
                draw_rect(ctx, ekFILL, xb, y0, w - xb, lane_h - LANE_GAP);

            draw_line_color(ctx, pal->axis);
            if (xb > xa)
                draw_line(ctx, xa, ymid, xb, ymid);

            n = i_envelope(app, gt, from, to, w, ymid, half, pts, 8192);
            if (n >= 2)
            {
                draw_line_color(ctx, gt->muted ? pal->hud_dim : gt->color);
                draw_line_width(ctx, 1);
                draw_polyline(ctx, FALSE, pts, n);
            }
        }

        /* Botones de la banda: silenciar y cerrar. */
        {
            real32_t bx, by;
            uint32_t b;
            for (b = 0; b < 2; ++b)
            {
                int hov;
                i_lane_btn(app, i, b, &bx, &by);
                hov = (app->hover_x >= bx && app->hover_x <= bx + BTN_W &&
                       app->hover_y >= by && app->hover_y <= by + BTN_H) ? 1 : 0;
                i_draw_btn(ctx, app, bx, by, (b == 0) ? "M" : "X",
                           (b == 0 && gt->muted) ? 1 : 0, hov,
                           (b == 0) ? pal->warn : pal->sel_edge);
            }
        }

        ssb_track_stats_get(gt->track, &st);
        if (app->compact == TRUE)
            bstd_sprintf(txt, sizeof(txt), "%s", gt->src.name);
        else if (gt->src.kind == ssb_src_process && gt->src.endpoint[0] != 0)
            /* Para una aplicacion se dice POR DONDE esta sonando. Es el dato
               que faltaba cuando una pista de app sale en silencio: puede estar
               renderizando a otra salida y no hay forma de verlo en la onda. */
            bstd_sprintf(txt, sizeof(txt), "%s  -  %s  ->  %s  |  %.1f MB  x%.1f  %u Hz %u bit",
                         gt->name, gt->src.name, gt->src.endpoint,
                         (double)st.disk_bytes / 1048576.0, st.ratio, st.rate,
                         ssb_track_bits(gt->track));
        else
            bstd_sprintf(txt, sizeof(txt), "%s  -  %s  |  %.1f MB  x%.1f  %u Hz %u bit",
                         gt->name, gt->src.name,
                         (double)st.disk_bytes / 1048576.0, st.ratio, st.rate,
                         ssb_track_bits(gt->track));
        draw_text_color(ctx, gt->muted ? pal->warn : pal->hud_text);
        draw_font(ctx, app->font);
        draw_text(ctx, txt, 6 + 2 * (BTN_W + 4.0f) + 4.0f, y0 + 3);
        if (gt->muted == TRUE)
        {
            char_t mk[64];
            bstd_sprintf(mk, sizeof(mk), "  [%s]", T(app, TXT_MUTED));
            draw_text_color(ctx, pal->warn);
            draw_text(ctx, mk, 6 + 2 * (BTN_W + 4.0f) + 4.0f + (real32_t)str_len_c(txt) * 5.6f, y0 + 3);
        }
        /* La fuente ya no existe. Es lo primero que hay que decir: sin esto, la
           banda se queda vacia y parece que el fallo es de la grabacion. */
        if (gt->gone == TRUE && app->compact == FALSE)
        {
            draw_text_color(ctx, pal->warn);
            draw_font(ctx, app->font);
            draw_text(ctx, T(app, TXT_SRC_GONE), 6 + 2 * (BTN_W + 4.0f) + 4.0f, y0 + 20);
        }
        else if (gt->sys_muted == TRUE && app->compact == FALSE)
        {
            draw_text_color(ctx, pal->warn);
            draw_text(ctx, T(app, TXT_SYS_MUTED), 6 + 2 * (BTN_W + 4.0f) + 4.0f, y0 + 20);
        }

        /* Medidor de nivel.
         *
         * Muestra el nivel de AHORA, no el pico historico: lo segundo se queda
         * lleno en cuanto suena algo una vez y deja de informar justo cuando
         * mas falta hace — cuando una fuente ha dejado de entregar audio y uno
         * esta intentando averiguar por que graba silencio.
         *
         * La escala es logaritmica sobre 60 dB. Lineal, una voz normal apenas
         * mueve la barra y todo parece muerto. */
        if (app->compact == FALSE)
        {
            real32_t bw = 96.0f;
            real32_t bx = w - bw - 8;
            real32_t lv, pk;
            char_t db[24];
            lv = (real32_t)i_meter_scale(st.level);
            pk = (real32_t)i_meter_scale(st.peak);
            draw_fill_color(ctx, pal->meter_bg);
            draw_rect(ctx, ekFILL, bx, y0 + 4, bw, 7);
            if (lv > 0)
            {
                /* Rojo pegado a fondo de escala: saturar se ve, no se deduce. */
                draw_fill_color(ctx, (st.level >= 0.99) ? pal->warn
                                                        : (gt->muted ? pal->hud_dim : gt->color));
                draw_rect(ctx, ekFILL, bx, y0 + 4, bw * lv, 7);
            }
            /* Marca del pico historico, para no perderlo de vista. */
            if (pk > 0)
            {
                draw_fill_color(ctx, pal->hud_text);
                draw_rect(ctx, ekFILL, bx + bw * pk - 1.0f, y0 + 3, 2, 9);
            }
            draw_font(ctx, app->font);
            draw_text_color(ctx, st.level > 0.0001 ? pal->hud_text : pal->warn);
            if (st.level > 0.0001)
                bstd_sprintf(db, sizeof(db), "%.0f dB", 20.0 * log10(st.level));
            else
                str_copy_c(db, sizeof(db), T(app, TXT_NO_SIGNAL));
            draw_text(ctx, db, bx - 62, y0 + 2);
        }
    }

    /* Borde vivo del buffer, mientras aun no ha llegado al tope. */
    if (xlive < w - 1.0f)
    {
        draw_line_color(ctx, pal->ruler_line);
        draw_line(ctx, xlive, i_top_h(app), xlive, i_top_h(app) + (real32_t)app->ntracks * lane_h);
    }

    /* Cabeza de reproduccion. Se dibuja sobre la seleccion, que es de donde
       sale lo que suena: sin ella no hay forma de saber por donde va la
       escucha, y con una seleccion larga eso se nota enseguida. */
    if (app->play != NULL)
    {
        /* Respecto a donde EMPEZO la reproduccion, no a la seleccion viva: la
           seleccion se rehace con cada clic y la cabeza acababa dando saltos
           que no correspondian a nada de lo que sonaba. */
        ssb_time a = app->play_from;
        ssb_time at = a + (ssb_time)(ssb_play_position(app->play) * (double)SSB_TICKS_PER_SEC);
        real32_t xp = 0;
        wave_time_to_x(app, at, w, &xp);
        if (xp >= 0 && xp <= w)
        {
            draw_line_color(ctx, pal->ok);
            draw_line(ctx, xp, i_top_h(app), xp, i_top_h(app) + (real32_t)app->ntracks * lane_h);
        }
    }

    if (app->has_sel == TRUE)
    {
        ssb_time a = (app->sel_a < app->sel_b) ? app->sel_a : app->sel_b;
        ssb_time b = (app->sel_a < app->sel_b) ? app->sel_b : app->sel_a;
        real32_t xa, xb;
        wave_time_to_x(app, a, w, &xa);
        wave_time_to_x(app, b, w, &xb);
        if (xb > xa)
        {
            real32_t sy = i_top_h(app);
            real32_t sh = (real32_t)app->ntracks * lane_h;
            draw_fill_color(ctx, pal->sel_fill);
            draw_rect(ctx, ekFILL, xa, sy, xb - xa, sh);
            draw_line_color(ctx, pal->sel_edge);
            draw_line(ctx, xa, sy, xa, sy + sh);
            draw_line(ctx, xb, sy, xb, sy + sh);
        }
    }

    if (app->compact == FALSE)
        i_draw_ruler(ctx, app, w, top, live);
    i_draw_hud(ctx, app, w, top);
    i_draw_console(ctx, app, w, top);
    /* La ultima, para que quede por encima de todo lo demas. */
    i_draw_keyhelp(ctx, app, w, top);

    {
        double ms = (double)(ssb_now() - t_ini) / 10000.0;
        app->draw_ms_sum += ms;
        app->draw_frames++;
        if (ms > app->draw_ms_max)
            app->draw_ms_max = ms;
    }
}

/* ------------------------------------------------------------ interaccion */

/* Mantiene la ventana visible dentro de lo que existe. Mientras el buffer se
   llena queda anclada al principio; cuando esta lleno, al final. */
/* La ventana de tiempo, con UNA sola regla:
 *
 *     ventana = [ahora - buffer, ahora]
 *
 * El ancho del lienzo es el buffer, siempre, y el borde derecho es el instante
 * mas reciente. Mientras se llena, la parte sin datos se ve a la izquierda; una
 * vez lleno, la onda ocupa todo el ancho y se traslada.
 *
 * Antes habia dos modos (anclado al principio mientras llenaba, anclado al ahora
 * en regimen) y el salto entre ellos era justo eso, un salto. Una regla sola no
 * puede saltar.
 */
void wave_clamp(App *app)
{
    ssb_time from = 0, to = 0;
    ssb_time span;
    double buf = app_buffer_secs(app);

    if (app->span_secs < 0.2)
        app->span_secs = 0.2;
    if (app->span_secs > buf)
        app->span_secs = buf;

    if (app_span(app, &from, &to) == FALSE)
        return;
    app->live = to;

    if (app->frozen == TRUE)
        return; /* congelada: no se toca nada de la ventana */

    if (app->zoomed == FALSE)
        app->span_secs = buf;
    span = (ssb_time)(app->span_secs * (double)SSB_TICKS_PER_SEC);

    if (app->follow == TRUE)
        app->win_to = to;
    if (app->win_to > to)
        app->win_to = to;
    /* Por la izquierda no se fuerza nada: si la ventana es mas ancha que lo
       grabado, ese hueco se dibuja como vacio, que es la verdad. */
    if (app->win_to < span)
        app->win_to = span;
}

static void i_OnDown(App *app, Event *e)
{
    const EvMouse *p = event_params(e, EvMouse);
    S2Df sz;
    V2Df vp;
    view_viewport(app->view, &vp, &sz);
    if (p->button == ekGUI_MOUSE_RIGHT)
    {
        app->panning = TRUE;
        app->pan_x = p->x;
        app->follow = FALSE;
        return;
    }
    /* Los botones de la banda se comen la pulsacion antes que la seleccion. */
    {
        real32_t rel = p->y - i_top_h(app);
        if (rel >= 0 && app->ntracks > 0)
        {
            uint32_t idx = (uint32_t)(rel / i_lane_h(app));
            if (idx < app->ntracks)
            {
                real32_t bx, by;
                uint32_t b;
                for (b = 0; b < 2; ++b)
                {
                    i_lane_btn(app, idx, b, &bx, &by);
                    if (p->x >= bx && p->x <= bx + BTN_W && p->y >= by && p->y <= by + BTN_H)
                    {
                        if (b == 0)
                            app_toggle_mute(app, idx);
                        else
                            app_close_track(app, idx);
                        return;
                    }
                }
            }
        }
    }
    /* Se guarda la seleccion antes de empezar: si esto acaba siendo un clic
       seco durante la reproduccion, hay que devolverla como estaba. */
    app->sel_before_a = app->sel_a;
    app->sel_before_b = app->sel_b;
    app->had_sel = app->has_sel;
    app->pending_seek = (app->play != NULL) ? TRUE : FALSE;
    app->selecting = TRUE;
    app->has_sel = TRUE;
    app->sel_a = i_clamp_to_buffer(app, wave_x_to_time(app, p->x, sz.width));
    app->sel_b = app->sel_a;
    view_update(app->view);
}

static void i_OnMove(App *app, Event *e)
{
    const EvMouse *p = event_params(e, EvMouse);
    S2Df sz;
    V2Df vp;
    view_viewport(app->view, &vp, &sz);

    /* Donde esta el raton, para resaltar el boton que tenga debajo. Solo se
       repinta si de verdad ha cambiado de sitio: pedir un repintado por cada
       mensaje de movimiento costaria mas que todo lo que se dibuja. */
    if (p->x != app->hover_x || p->y != app->hover_y)
    {
        app->hover_x = p->x;
        app->hover_y = p->y;
        view_update(app->view);
    }

    if (app->panning == TRUE)
    {
        double per_px = app->span_secs / (double)sz.width;
        double dt = (double)(app->pan_x - p->x) * per_px;
        ssb_time shift = (ssb_time)((dt < 0 ? -dt : dt) * (double)SSB_TICKS_PER_SEC);
        if (dt > 0)
            app->win_to += shift;
        else if (app->win_to > shift)
            app->win_to -= shift;
        app->pan_x = p->x;
        wave_clamp(app);
        view_update(app->view);
        return;
    }
    /* EvWheel no trae modificadores, pero EvMouse si: se cachea aqui y la rueda
       lo consulta. Cualquier movimiento del raton lo mantiene al dia. */
    app->ctrl_down = ((p->modifiers & ekMKEY_CONTROL) != 0) ? TRUE : FALSE;

    if (app->selecting == TRUE)
    {
        app->sel_b = i_clamp_to_buffer(app, wave_x_to_time(app, p->x, sz.width));
        view_update(app->view);
    }
}

static void i_OnUp(App *app, Event *e)
{
    const EvMouse *p = event_params(e, EvMouse);
    unref(p);
    app->selecting = FALSE;
    app->panning = FALSE;
    if (app->sel_a == app->sel_b)
    {
        /* Un clic sin arrastre. Si hay algo sonando, es una peticion de saltar
           ahi: arrastrar sigue siendo marcar el tramo y el clic seco mueve la
           escucha, que es la unica forma de que los dos gestos convivan sin
           pisarse. Y la seleccion NO se toca, que es lo que hacia que la
           cabeza saltara a sitios sin sentido. */
        if (app->play != NULL && app->pending_seek == TRUE)
        {
            double off = ssb_time_to_sec((app->sel_a > app->play_from)
                                             ? (app->sel_a - app->play_from) : 0);
            ssb_play_seek(app->play, off);
            app->sel_a = app->sel_before_a;
            app->sel_b = app->sel_before_b;
            app->has_sel = app->had_sel;
        }
        else
        {
            app->has_sel = FALSE;
        }
    }
    app->pending_seek = FALSE;
    view_update(app->view);
}

/* Zoom por factor, alrededor del centro de la ventana visible. */
void wave_zoom(App *app, double factor)
{
    double buf = app_buffer_secs(app);
    app->span_secs *= factor;
    if (app->span_secs >= buf)
    {
        /* Alejarse mas alla del buffer no ensena nada: no hay nada que ensenar. */
        app->span_secs = buf;
        app->follow = TRUE;
        app->zoomed = FALSE;
    }
    else
    {
        if (app->span_secs < 0.2)
            app->span_secs = 0.2;
        app->zoomed = TRUE;
    }
    wave_clamp(app);
    view_update(app->view);
}

/* La rueda hace SCROLL de la lista de pistas, que es lo que espera cualquiera.
   El zoom va con Ctrl+rueda y con los botones +/-. */
static void i_OnWheel(App *app, Event *e)
{
    const EvWheel *p = event_params(e, EvWheel);
    S2Df sz;
    V2Df vp;

    view_viewport(app->view, &vp, &sz);

    if (app->ctrl_down == TRUE)
    {
        ssb_time under = wave_x_to_time(app, p->x, sz.width);
        double factor = (p->dy > 0) ? (1.0 / 1.25) : 1.25;
        double before = app->span_secs;
        wave_zoom(app, factor);
        /* Mantener bajo el cursor el mismo instante que habia antes del zoom. */
        if (sz.width > 0 && app->span_secs < app_buffer_secs(app) && app->span_secs != before)
        {
            double frac = (double)p->x / (double)sz.width;
            double span = app->span_secs * (double)SSB_TICKS_PER_SEC;
            double to = (double)under + (1.0 - frac) * span;
            app->win_to = (to < 0) ? 0 : (ssb_time)to;
            app->follow = FALSE;
            wave_clamp(app);
            view_update(app->view);
        }
        return;
    }

    {
        real32_t step = i_lane_h(app) * 0.5f;
        real32_t y = vp.y - (real32_t)p->dy * step;
        real32_t maxy = i_top_h(app) + (real32_t)app->ntracks * i_lane_h(app) - sz.height;
        if (maxy < 0)
            maxy = 0;
        if (y < 0)
            y = 0;
        if (y > maxy)
            y = maxy;
        view_scroll_y(app->view, y);
        view_update(app->view);
    }
}

/* Ajusta el lienzo al numero de pistas. Se llama al anadir o quitar una pista y
   al redimensionar la ventana. */
void wave_resize(App *app)
{
    S2Df vsize;
    V2Df vpos;
    real32_t hh;
    if (app->view == NULL)
        return;
    view_viewport(app->view, &vpos, &vsize);
    hh = i_top_h(app) + (real32_t)app->ntracks * i_lane_h(app);
    if (hh < vsize.height)
        hh = vsize.height;
    /* Solo se toca el lienzo si de verdad cambia: `view_content_size` puede
       mover las barras de desplazamiento, y eso vuelve a disparar OnSize. */
    if (hh == app->content_h && vsize.width == app->content_w)
        return;
    app->content_h = hh;
    app->content_w = vsize.width;
    view_content_size(app->view, s2df(vsize.width, hh), s2df(10, 20));
}

static void i_OnSize(App *app, Event *e)
{
    unref(e);
    wave_resize(app);
}

View *wave_create(App *app)
{
    View *view = view_scroll();
    view_size(view, s2df(900, 420));
    view_scroll_visible(view, FALSE, TRUE);
    view_OnDraw(view, listener(app, i_OnDraw, App));
    view_OnDown(view, listener(app, i_OnDown, App));
    view_OnUp(view, listener(app, i_OnUp, App));
    view_OnMove(view, listener(app, i_OnMove, App));
    view_OnDrag(view, listener(app, i_OnMove, App));
    view_OnWheel(view, listener(app, i_OnWheel, App));
    view_OnSize(view, listener(app, i_OnSize, App));
    return view;
}
