/* Modo comandos: la aplicacion entera desde el teclado.
 *
 * La idea es que no haga falta el raton para nada: anadir fuentes, grabar,
 * parar, recortar, guardar. Cada comando hace exactamente lo mismo que su boton,
 * llamando a las mismas funciones — no hay un camino "de comandos" paralelo que
 * pueda divergir del de la interfaz.
 */
#include "ssbgui.h"
#include <stdlib.h>
#include <stdio.h>

/* Escribe una linea en la consola del lienzo. */
void cmd_print(App *app, const char_t *fmt, ...)
{
    va_list ap;
    uint32_t i;
    /* Se desplaza hacia arriba: la ultima linea es la mas reciente. */
    for (i = 0; i + 1 < CMD_LINES; ++i)
        str_copy_c(app->cmdout[i], sizeof(app->cmdout[i]), app->cmdout[i + 1]);
    va_start(ap, fmt);
    bstd_vsprintf(app->cmdout[CMD_LINES - 1], sizeof(app->cmdout[0]), fmt, ap);
    va_end(ap);
    /* Eco a fichero cuando corre un guion. Una prueba tiene que poder leer lo
       que dijo el programa sin depender de que su ventana estuviera visible. */
    if (app->script_log[0] != 0)
    {
        FILE *lg = fopen(app->script_log, "ab");
        if (lg != NULL)
        {
            fprintf(lg, "%s\n", app->cmdout[CMD_LINES - 1]);
            fclose(lg);
        }
    }
    view_update(app->view);
}

static void i_help(App *app)
{
    cmd_print(app, "rec | stop | add <fuente> | close <n> | mute <n> | solo <n>");
    cmd_print(app, "buffer <seg> | zoom <seg> | sel <seg> | all | save [seg]");
    cmd_print(app, "export wav|mp3|m4a | folder <ruta>|default | list | tracks");
    cmd_print(app, "play (reproduce/pausa) | hush | mix on|off | quality 16|24");
    cmd_print(app, "keys (o F1): la lista de comandos de teclado");
    cmd_print(app, "fuente: output | output:<n> | input | app:<nombre> | app:<pid>");
}

static void i_list_sources(App *app)
{
    uint32_t i, o = 0, in = 0;
    for (i = 0; i < app->nsources && i < 12; ++i)
    {
        const ssb_source *s = &app->list[i];
        if (s->kind == ssb_src_output_device)
            cmd_print(app, "output:%u  %s", o++, s->name);
        else if (s->kind == ssb_src_input_device)
            cmd_print(app, "input:%u   %s", in++, s->name);
        else
            cmd_print(app, "app:%u  %-22s %s -> %s", s->pid, s->name,
                      s->active ? "SUENA " : "callada", s->endpoint);
    }
    if (app->nsources > 12)
        cmd_print(app, "... y %u mas", app->nsources - 12);
}

static void i_list_tracks(App *app)
{
    uint32_t i;
    if (app->ntracks == 0)
    {
        cmd_print(app, "sin pistas");
        return;
    }
    {
        ssb_time uf = 0, ut = 0;
        if (app_span(app, &uf, &ut) == TRUE)
            cmd_print(app, "seleccionable: %.2f s (union de todas)", ssb_time_to_sec(ut - uf));
    }
    for (i = 0; i < app->ntracks; ++i)
    {
        ssb_track_stats st;
        ssb_time a = 0, b = 0;
        double cov = 0.0;
        ssb_track_stats_get(app->tracks[i].track, &st);
        if (ssb_track_span(app->tracks[i].track, &a, &b) == ssb_ok)
            cov = ssb_time_to_sec(b - a);
        /* `huecos` es lo unico que distingue "la fuente perdio audio" de
           "todo bien": si sale 0, ningun silencio de la exportacion es nuestro. */
        cmd_print(app, "%u  %s  %u Hz  %.1f MB  x%.1f  cubre %.2f s de %u  huecos %u%s",
                  i + 1, app->tracks[i].src.name, st.rate,
                  (double)st.disk_bytes / 1048576.0, st.ratio, cov,
                  ssb_track_buffer_seconds(app->tracks[i].track),
                  st.reanchors,
                  app->tracks[i].muted ? "  [MUTED]" : "");
    }
}

/* Parte "verbo resto" sin tocar la cadena original. */
static void i_split(const char_t *line, char_t *verb, uint32_t vsize, const char_t **rest)
{
    uint32_t i = 0;
    while (line[i] == ' ')
        i++;
    {
        uint32_t j = 0;
        while (line[i] != 0 && line[i] != ' ' && j + 1 < vsize)
            verb[j++] = line[i++];
        verb[j] = 0;
    }
    while (line[i] == ' ')
        i++;
    *rest = &line[i];
}

void cmd_run(App *app, const char_t *line)
{
    char_t verb[32];
    const char_t *arg;

    if (line == NULL || line[0] == 0)
        return;
    i_split(line, verb, sizeof(verb), &arg);
    cmd_print(app, "> %s", line);

    if (str_equ_c(verb, "help") == TRUE || str_equ_c(verb, "?") == TRUE)
    {
        i_help(app);
    }
    else if (str_equ_c(verb, "rec") == TRUE || str_equ_c(verb, "stop") == TRUE)
    {
        int want = (str_equ_c(verb, "rec") == TRUE) ? TRUE : FALSE;
        if (app->recording != want)
            app_set_recording(app, want);
        cmd_print(app, app->recording ? "grabando" : "parado");
    }
    else if (str_equ_c(verb, "add") == TRUE)
    {
        ssb_source src;
        if (arg[0] == 0)
            cmd_print(app, "falta la fuente. prueba: list");
        else if (ssb_source_parse(arg, &src) != ssb_ok)
            cmd_print(app, "no encuentro esa fuente: %s", arg);
        else if (app_add_track(app, &src) == FALSE)
            cmd_print(app, "no se pudo abrir esa fuente");
        else
            cmd_print(app, "pista %u: %s", app->ntracks, src.name);
    }
    else if (str_equ_c(verb, "close") == TRUE)
    {
        uint32_t n = (uint32_t)atoi(arg);
        if (n == 0 || n > app->ntracks)
            cmd_print(app, "pista fuera de rango");
        else
        {
            app_close_track(app, n - 1);
            cmd_print(app, "cerrada la pista %u", n);
        }
    }
    else if (str_equ_c(verb, "mute") == TRUE)
    {
        uint32_t n = (uint32_t)atoi(arg);
        if (n == 0 || n > app->ntracks)
            cmd_print(app, "pista fuera de rango");
        else
        {
            app_toggle_mute(app, n - 1);
            cmd_print(app, "pista %u: %s", n, app->tracks[n - 1].muted ? "muda" : "activa");
        }
    }
    else if (str_equ_c(verb, "solo") == TRUE)
    {
        uint32_t n = (uint32_t)atoi(arg), i;
        if (n == 0 || n > app->ntracks)
            cmd_print(app, "pista fuera de rango");
        else
        {
            for (i = 0; i < app->ntracks; ++i)
                app->tracks[i].muted = (i == n - 1) ? FALSE : TRUE;
            cmd_print(app, "solo la pista %u", n);
            view_update(app->view);
        }
    }
    else if (str_equ_c(verb, "buffer") == TRUE)
    {
        uint32_t secs = (uint32_t)atoi(arg);
        if (secs < 1)
            cmd_print(app, "buffer <segundos>");
        else
        {
            app_set_buffer(app, secs);
            cmd_print(app, "buffer: %u s en %u pista(s)", secs, app->ntracks);
        }
    }
    else if (str_equ_c(verb, "zoom") == TRUE)
    {
        double secs = atof(arg);
        if (secs <= 0.0)
        {
            app->zoomed = FALSE;
            app->follow = TRUE;
            cmd_print(app, "zoom: al buffer entero");
        }
        else
        {
            app->span_secs = secs;
            app->zoomed = TRUE;
            cmd_print(app, "zoom: %.2f s", secs);
        }
        wave_clamp(app);
        view_update(app->view);
    }
    else if (str_equ_c(verb, "all") == TRUE)
    {
        app_select_all(app);
        cmd_print(app, "seleccionado todo el buffer");
    }
    else if (str_equ_c(verb, "sel") == TRUE)
    {
        double secs = atof(arg);
        if (secs <= 0.0)
            cmd_print(app, "sel <segundos>  (los ultimos N)");
        else
        {
            app_select_last(app, secs);
            if (app->has_sel == TRUE)
                cmd_print(app, "seleccion: %.2f s", ssb_time_to_sec(app->sel_b - app->sel_a));
            else
                cmd_print(app, "no hay nada grabado");
        }
    }
    else if (str_equ_c(verb, "save") == TRUE)
    {
        double secs = atof(arg);
        if (secs > 0.0)
            app_select_last(app, secs);
        app_quick_save(app);
    }
    else if (str_equ_c(verb, "export") == TRUE)
    {
        uint32_t f = 0;
        if (str_equ_c(arg, "mp3") == TRUE)
            f = 1;
        else if (str_equ_c(arg, "m4a") == TRUE || str_equ_c(arg, "aac") == TRUE)
            f = 2;
        popup_selected(app->export_fmt, f);
        app_save_settings(app);
        cmd_print(app, "formato de salida: %s", f == 1 ? "mp3" : (f == 2 ? "m4a" : "wav"));
    }
    else if (str_equ_c(verb, "keys") == TRUE || str_equ_c(verb, "teclas") == TRUE)
    {
        /* Lo mismo que F1. Existe tambien como orden porque un atajo que solo
           se puede probar pulsandolo no se puede probar automaticamente. */
        app->keyhelp = (app->keyhelp == TRUE) ? FALSE : TRUE;
        view_update(app->view);
        cmd_print(app, app->keyhelp ? "chuleta de atajos: visible" : "chuleta de atajos: oculta");
    }
    else if (str_equ_c(verb, "play") == TRUE)
    {
        app_play_toggle(app);
        if (app->play == NULL)
            cmd_print(app, "no hay nada que reproducir");
        else
            cmd_print(app, "%s  (%.2f s)",
                      ssb_play_paused(app->play) ? "pausa" : "reproduciendo",
                      ssb_play_duration(app->play));
    }
    else if (str_equ_c(verb, "hush") == TRUE || str_equ_c(verb, "pstop") == TRUE)
    {
        app_play_stop(app);
        cmd_print(app, "reproduccion parada");
    }
    else if (str_equ_c(verb, "mix") == TRUE)
    {
        if (str_equ_c(arg, "off") == TRUE)
            app->mix_export = FALSE;
        else if (str_equ_c(arg, "on") == TRUE || arg[0] == 0)
            app->mix_export = TRUE;
        app_save_settings(app);
        app_relabel(app);
        cmd_print(app, app->mix_export ? "exportar: todo en un fichero"
                                       : "exportar: un fichero por pista");
    }
    else if (str_equ_c(verb, "quality") == TRUE || str_equ_c(verb, "calidad") == TRUE)
    {
        /* Solo afecta a las pistas que se anadan A PARTIR de ahora: cambiar la
           resolucion de un anillo ya escrito significaria reinterpretar bloques
           que estan guardados con otra, y eso es peor que no dejar hacerlo. */
        uint32_t bits = (uint32_t)atoi(arg);
        uint32_t cur = popup_get_selected(app->compress) % 4;
        int raw = (cur == 1 || cur == 3);
        if (bits != 16 && bits != 24)
        {
            cmd_print(app, "quality 16 | 24   (se aplica a las pistas nuevas)");
        }
        else
        {
            uint32_t q = (bits == 24) ? (raw ? 3u : 2u) : (raw ? 1u : 0u);
            popup_selected(app->compress, q);
            app_save_settings(app);
            cmd_print(app, "resolucion: %u bits%s (para pistas nuevas)",
                      bits, raw ? ", sin comprimir" : "");
        }
    }
    else if (str_equ_c(verb, "folder") == TRUE)
    {
        if (arg[0] == 0 || str_equ_c(arg, "default") == TRUE)
        {
            str_copy_c(app->savedir, sizeof(app->savedir), app->dir);
        }
        else
        {
            /* Absoluta siempre: un dialogo de fichero mueve el directorio de
               trabajo del proceso y una ruta relativa acabaria en otro sitio. */
            ssb_abs_path(arg, app->savedir, sizeof(app->savedir));
            ssb_mkdir(app->savedir);
        }
        app_save_settings(app);
        cmd_print(app, "se guarda en: %s", app->savedir);
    }
    else if (str_equ_c(verb, "list") == TRUE)
    {
        app_reload_sources(app);
        i_list_sources(app);
    }
    else if (str_equ_c(verb, "perf") == TRUE)
    {
        if (app->draw_frames == 0)
            cmd_print(app, "aun no se ha pintado nada");
        else
            cmd_print(app, "dibujo del lienzo: %.3f ms de media, %.3f ms el peor, %u fotogramas",
                      app->draw_ms_sum / (double)app->draw_frames,
                      app->draw_ms_max, app->draw_frames);
        app->draw_ms_sum = 0.0;
        app->draw_ms_max = 0.0;
        app->draw_frames = 0;
    }
    else if (str_equ_c(verb, "tracks") == TRUE)
    {
        i_list_tracks(app);
    }
    else
    {
        cmd_print(app, "no conozco '%s'. prueba: help", verb);
    }
}
