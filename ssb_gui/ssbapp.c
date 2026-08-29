/* SystemSoundBuffer â€” interfaz.
 *
 * Consume ssb_core y no sabe nada de WASAPI. El refresco va por `osmain_sync`,
 * que llama a i_update en el HILO DE INTERFAZ a la cadencia pedida; los hilos de
 * captura son del motor y aqui no se tocan.
 */
#include "ssbgui.h"
#include <stdlib.h>
#include <stdio.h>
/* strchr. Antes llegaba de rebote por <Windows.h>, que ya no se incluye. */
#include <string.h>

/* Tamanos de buffer, en segundos. Los cortos sirven para trabajar comodo: con
   10 s el circular se ve funcionar en diez segundos. */
static const uint32_t i_BUFFER_SECS[9] = { 10, 30, 60, 120, 300, 900, 1800, 3600, 7200 };
static const char_t *i_BUFFER_TEXT[9] = { "10 s", "30 s", "1 min", "2 min", "5 min",
                                          "15 min", "30 min", "1 h", "2 h" };
#define BUFFER_N 9

const char_t *T(const App *app, ssb_txt id)
{
    return SSB_TEXT[app->lang % LANG_COUNT][id];
}

/* Definidas mas abajo; las necesitan funciones que van antes. */
static void i_play_render(App *app, SsbJob *job);
static void i_save_all(App *app, const char_t *base, int mixed, SsbJob *job);
static bool_t i_job_start(App *app, const char_t *base, int mixed, int para_oir);
static void i_job_collect(App *app);
static void i_script_load(App *app, const char_t *path);

/* Mensaje efimero, se dibuja en el lienzo y se apaga solo. */
static void i_say(App *app, const char_t *text)
{
    str_copy_c(app->message, sizeof(app->message), text);
    app->message_at = 0.0;
    /* Y al registro del guion, si lo hay: los mensajes importantes (el
       resultado de guardar, entre otros) salen por aqui y no por la consola,
       asi que sin esto una prueba no puede saber si funciono. */
    if (app->script_log[0] != 0)
    {
        FILE *lg = fopen(app->script_log, "ab");
        if (lg != NULL)
        {
            fprintf(lg, "[msg] %s\n", text);
            fclose(lg);
        }
    }
}

bool_t app_filling(App *app)
{
    uint32_t i;
    for (i = 0; i < app->ntracks; ++i)
    {
        ssb_track_stats st;
        ssb_track_stats_get(app->tracks[i].track, &st);
        if (st.dropped > 0)
            return FALSE;
    }
    return TRUE;
}

/* El ancho de la vista sale de lo que dicen las pistas, no de una copia que
   guarde la interfaz. Si algun dia una pista cambia de tamano por su cuenta, la
   vista se entera sola. Sin pistas, manda el valor por omision del desplegable. */
double app_buffer_secs(App *app)
{
    uint32_t i;
    uint32_t best = 0;
    for (i = 0; i < app->ntracks; ++i)
    {
        uint32_t s = ssb_track_buffer_seconds(app->tracks[i].track);
        if (s > best)
            best = s;
    }
    return (best > 0) ? (double)best : app->buffer_secs;
}

/* Tramo que se puede mirar y seleccionar: la UNION de lo que cubre cada pista.
 *
 * Fue la interseccion hasta que se vio lo que significaba: anadir una pista a
 * mitad de la grabacion hacia desaparecer todo lo anterior a ella. No se podia
 * seleccionar, y cualquier seleccion que ya hubiera pasaba a estar "fuera de lo
 * que cubren todas las pistas" â€” un mensaje que no apuntaba en absoluto a la
 * causa.
 *
 * La interseccion existia por una razon real: que al exportar todas las pistas
 * durasen lo mismo. Pero eso se arregla donde de verdad estaba el problema, en
 * la exportacion, que ahora escribe el tramo entero y rellena con silencio lo
 * que una pista no cubra. Lo que esa pista estaba grabando entonces era, de
 * hecho, nada. */
bool_t app_span(App *app, ssb_time *from, ssb_time *to)
{
    uint32_t i;
    int have = 0;
    ssb_time f = 0, t = 0;
    for (i = 0; i < app->ntracks; ++i)
    {
        ssb_time a = 0, b = 0;
        if (ssb_track_span(app->tracks[i].track, &a, &b) != ssb_ok)
            continue;
        if (!have)
        {
            f = a;
            t = b;
            have = 1;
        }
        else
        {
            if (a < f)
                f = a;
            if (b > t)
                t = b;
        }
    }
    if (!have || t <= f)
        return FALSE;
    if (from != NULL)
        *from = f;
    if (to != NULL)
        *to = t;
    return TRUE;
}

/* ------------------------------------------------------------------ fuentes */

void app_reload_sources(App *app)
{
    uint32_t i, idx_out = 0, idx_in = 0;
    popup_clear(app->sources);
    /* De la foto del vigilante: enumerar aqui bloquearia el hilo de interfaz.
       Si el vigilante aun no ha publicado nada, se enumera una vez para no
       arrancar con el desplegable vacio. */
    app->nsources = ssb_watch_sources(app->list, GUI_MAX_SOURCES, &app->src_version);
    if (app->nsources == 0)
        app->nsources = ssb_enumerate(app->list, GUI_MAX_SOURCES);
    if (app->nsources > GUI_MAX_SOURCES)
        app->nsources = GUI_MAX_SOURCES;
    for (i = 0; i < app->nsources; ++i)
    {
        char_t txt[SSB_NAME_MAX * 2 + 64];
        const ssb_source *s = &app->list[i];
        if (s->kind == ssb_src_output_device)
            bstd_sprintf(txt, sizeof(txt), "%s %u  -  %s",
                         app->lang == LANG_EN ? "Output" : "Salida", idx_out++, s->name);
        else if (s->kind == ssb_src_input_device)
            bstd_sprintf(txt, sizeof(txt), "%s %u  -  %s",
                         app->lang == LANG_EN ? "Input" : "Entrada", idx_in++, s->name);
        else
            /* Se dice por que salida esta sonando: una app puede estar
               renderizando a otro dispositivo del que capturas, y entonces
               grabas silencio sin ninguna pista de por que. */
            bstd_sprintf(txt, sizeof(txt), "App  -  %s (pid %u)%s%s", s->name, s->pid,
                         s->active ? "  *  " : "  -  ", s->endpoint);
        popup_add_elem(app->sources, txt, NULL);
    }
    if (app->nsources > 0)
        popup_selected(app->sources, 0);
}

/* ------------------------------------------------------------------- pistas */

bool_t app_add_track(App *app, const ssb_source *src)
{
    ssb_track_config cfg;
    char_t dir[512];
    GTrack *gt;

    if (app->ntracks >= GUI_MAX_TRACKS)
        return FALSE;

    ssb_track_config_default(&cfg);
    /* De `buffer_secs` y no del desplegable. Un valor a medida —los que solo
       se alcanzan por orden escrita— no tiene preajuste que marcar, asi que el
       desplegable se queda donde estaba; leerlo aqui hacia que la pista nueva
       naciera con el valor viejo. Se veia en el registro: `buffer 20`, y la
       pista siguiente con 120. */
    cfg.max_seconds = (uint32_t)(app->buffer_secs + 0.5);
    if (cfg.max_seconds == 0)
        cfg.max_seconds = i_BUFFER_SECS[popup_get_selected(app->buffer) % BUFFER_N];
    cfg.max_bytes = 2048ull * 1024ull * 1024ull;
    /* Un solo desplegable para las dos decisiones que van juntas: comprimir o
       no, y con cuanta resolucion. Separarlas en dos controles invitaba a
       elegir combinaciones sin sentido y ocupaba el doble de barra.
         0 sin perdida 16   1 crudo 16   2 sin perdida 24   3 crudo 24 */
    {
        uint32_t q = popup_get_selected(app->compress) % 4;
        cfg.compress = (q == 0 || q == 2) ? 1 : 0;
        cfg.bits = (q >= 2) ? 24u : 16u;
    }

    gt = &app->tracks[app->ntracks];
    gt->src = *src;
    /* `ssb_source_parse` no rellena endpoint ni active (los saca la
       enumeracion), asi que se completan desde la lista si esta ahi. Sin esto,
       una pista creada por comando no sabria por donde suena su aplicacion. */
    if (src->kind == ssb_src_process && src->endpoint[0] == 0)
    {
        uint32_t q;
        for (q = 0; q < app->nsources; ++q)
        {
            if (app->list[q].kind == ssb_src_process && app->list[q].pid == src->pid)
            {
                gt->src = app->list[q];
                break;
            }
        }
    }
    gt->color = wave_track_color(app, app->ntracks);
    gt->muted = FALSE;
    bstd_sprintf(gt->name, sizeof(gt->name), "%s %u",
                 app->lang == LANG_EN ? "Track" : "Pista", app->ntracks + 1);
    /* El numero del directorio NO puede ser el indice de la pista: al cerrar
       una del medio, las siguientes se desplazan y una pista nueva reutilizaria
       la carpeta de otra que sigue viva. Dos anillos escribiendo los mismos
       `seg-*.dat` se pisan, y lo que exportas sale corrupto o vacio. */
    app->track_seq++;
    bstd_sprintf(dir, sizeof(dir), "%s/pista%u", app->dir, app->track_seq);

    if (ssb_track_create(gt->name, dir, &gt->src, &cfg, &gt->track) != ssb_ok)
        return FALSE;
    /* Una pista recien anadida NO graba: espera a que se pulse Grabar. Asi se
       puede montar el conjunto de fuentes con calma antes de empezar. */
    ssb_track_pause(gt->track, app->recording ? FALSE : TRUE);

    app->ntracks++;
    app->follow = TRUE;
    /* El ancho del lienzo ES el buffer: al anadir la primera pista, la ventana
       de tiempo pasa a valer lo que dure su buffer. */
    app->buffer_secs = (double)cfg.max_seconds;
    if (app->ntracks == 1)
        app->span_secs = app->buffer_secs;
    return TRUE;
}

static void i_OnHush(App *app, Event *e)
{
    unref(e);
    app_play_stop(app);
}

static void i_OnKeyHelp(App *app, Event *e)
{
    unref(e);
    app->keyhelp = (app->keyhelp == TRUE) ? FALSE : TRUE;
    view_update(app->view);
}

static void i_OnPlay(App *app, Event *e)
{
    unref(e);
    app_play_toggle(app);
}

static void i_OnMix(App *app, Event *e)
{
    unref(e);
    app->mix_export = (app->mix_export == TRUE) ? FALSE : TRUE;
    app_save_settings(app);
    app_relabel(app);
}

static void i_OnAdd(App *app, Event *e)
{
    uint32_t sel = popup_get_selected(app->sources);
    unref(e);
    if (sel >= app->nsources)
        return;
    if (app_add_track(app, &app->list[sel]) == FALSE)
    {
        i_say(app, T(app, TXT_MSG_OPEN_FAILED));
        return;
    }
    wave_clamp(app);
    wave_resize(app);
    view_update(app->view);
}

void app_toggle_mute(App *app, uint32_t index)
{
    char_t msg[120];
    if (index >= app->ntracks)
        return;
    app->tracks[index].muted = app->tracks[index].muted ? FALSE : TRUE;
    bstd_sprintf(msg, sizeof(msg),
                 T(app, app->tracks[index].muted ? TXT_MSG_MUTED : TXT_MSG_UNMUTED),
                 index + 1);
    i_say(app, msg);
    view_update(app->view);
}

void app_close_track(App *app, uint32_t index)
{
    uint32_t i;
    if (index >= app->ntracks)
        return;
    ssb_track_destroy(&app->tracks[index].track);
    for (i = index; i + 1 < app->ntracks; ++i)
    {
        app->tracks[i] = app->tracks[i + 1];
        app->tracks[i].color = wave_track_color(app, i);
        bstd_sprintf(app->tracks[i].name, sizeof(app->tracks[i].name), "%s %u",
                     app->lang == LANG_EN ? "Track" : "Pista", i + 1);
    }
    app->ntracks--;
    app->has_sel = FALSE;
    wave_resize(app);
    view_update(app->view);
}

/* --------------------------------------------------------------- controles */

static void i_OnPause(App *app, Event *e)
{
    uint32_t i;
    unref(e);
    app->frozen = app->frozen ? FALSE : TRUE;
    app->follow = app->frozen ? FALSE : TRUE;
    for (i = 0; i < app->ntracks; ++i)
        ssb_track_freeze(app->tracks[i].track, app->frozen);
    button_text(app->btn_pause, T(app, app->frozen ? TXT_RESUME_VIEW : TXT_FREEZE_VIEW));
    view_update(app->view);
}

/* Arranca y para la grabacion de todas las pistas. Parado, los buffers quedan
   intactos: es lo que permite mirar y guardar sin que lo que buscas se salga
   del circular por atras. */
void app_set_recording(App *app, int recording)
{
    uint32_t i;
    app->recording = recording ? TRUE : FALSE;
    for (i = 0; i < app->ntracks; ++i)
        ssb_track_pause(app->tracks[i].track, app->recording ? FALSE : TRUE);
    /* Simbolo + texto: el simbolo se reconoce de un vistazo y el texto quita
       cualquier duda sobre que va a pasar al pulsarlo. */
    button_text(app->btn_input, T(app, app->recording ? TXT_STOP_REC : TXT_REC));
    button_image(app->btn_input, app->recording ? app->ico_stop : app->ico_rec);
    i_say(app, T(app, app->recording ? TXT_MSG_RESUMED : TXT_MSG_STOPPED));
    view_update(app->view);
}

static void i_OnInput(App *app, Event *e)
{
    unref(e);
    app_set_recording(app, app->recording ? FALSE : TRUE);
}

/* Cambiar el buffer afecta a las pistas que YA estan grabando: si no, el
   desplegable parecia no hacer nada. Reducir descarta lo que sobra por el
   principio; ampliar deja crecer desde ahora. */
/* Cambia el buffer de todas las pistas vivas y deja la interfaz coherente.
   La comparten el desplegable y la orden `buffer`: si cada uno hiciera lo suyo,
   escribir `buffer 45` dejaria el desplegable marcando otra cosa y los ajustes
   guardarian un valor que el usuario nunca eligio. */
void app_set_buffer(App *app, uint32_t secs)
{
    char_t msg[160];
    uint32_t i;
    uint32_t idx = BUFFER_N;

    if (secs == 0)
        return;
    /* Si el valor coincide con un preajuste, el desplegable lo refleja. Los
       valores a medida (solo alcanzables por orden) dejan el desplegable donde
       esta: mentir marcando otro seria peor que no marcar nada. */
    for (i = 0; i < BUFFER_N; ++i)
    {
        if (i_BUFFER_SECS[i] == secs)
            idx = i;
    }
    if (idx < BUFFER_N)
        popup_selected(app->buffer, idx);

    for (i = 0; i < app->ntracks; ++i)
        ssb_track_set_buffer(app->tracks[i].track, secs);
    app->buffer_secs = (double)secs;
    app->zoomed = FALSE;
    app->follow = TRUE;
    app->span_secs = app->buffer_secs;
    wave_clamp(app);
    if (app->ntracks > 0)
    {
        char_t what[32];
        if (idx < BUFFER_N)
            str_copy_c(what, sizeof(what), i_BUFFER_TEXT[idx]);
        else
            bstd_sprintf(what, sizeof(what), "%u s", secs);
        bstd_sprintf(msg, sizeof(msg), T(app, TXT_MSG_BUFFER_CHANGED), what);
        i_say(app, msg);
    }
    app_save_settings(app);
    view_update(app->view);
}

static void i_OnBuffer(App *app, Event *e)
{
    unref(e);
    app_set_buffer(app, i_BUFFER_SECS[popup_get_selected(app->buffer) % BUFFER_N]);
}

/* Carpeta de guardado. Con el boton derecho se vuelve a la de por omision. */
static void i_OnFolder(App *app, Event *e)
{
    const char_t *dir;
    char_t msg[420];
    char_t cwd[600];
    unref(e);
    ssb_abs_path(".", cwd, sizeof(cwd));
    dir = comwin_select_dir(app->window, T(app, TXT_FOLDER), app->savedir);
    ssb_set_cwd(cwd);
    if (dir == NULL)
        return;
    str_copy_c(app->savedir, sizeof(app->savedir), dir);
    app_save_settings(app);
    bstd_sprintf(msg, sizeof(msg), T(app, TXT_MSG_FOLDER), app->savedir);
    i_say(app, msg);
}

/* Vuelve a la carpeta de por omision, que es el propio directorio de buffers. */
static void i_OnFolderDefault(App *app, Event *e)
{
    char_t msg[420];
    unref(e);
    str_copy_c(app->savedir, sizeof(app->savedir), app->dir);
    app_save_settings(app);
    bstd_sprintf(msg, sizeof(msg), T(app, TXT_MSG_FOLDER), app->savedir);
    i_say(app, msg);
}

static void i_OnLive(App *app, Event *e)
{
    unref(e);
    app->follow = TRUE;
    app->zoomed = FALSE;
    view_update(app->view);
}

void app_select_all(App *app)
{
    ssb_time from = 0, to = 0;
    if (app_span(app, &from, &to) == FALSE)
        return;
    app->sel_a = from;
    app->sel_b = to;
    app->has_sel = TRUE;
    view_update(app->view);
}

/* Los ultimos `secs` segundos de lo grabado. */
void app_select_last(App *app, double secs)
{
    ssb_time f = 0, t = 0, want;
    if (secs <= 0.0 || app_span(app, &f, &t) == FALSE)
        return;
    want = (ssb_time)(secs * (double)SSB_TICKS_PER_SEC);
    app->sel_b = t;
    app->sel_a = (t > f + want) ? t - want : f;
    app->has_sel = TRUE;
    view_update(app->view);
}

static void i_OnSelectAll(App *app, Event *e)
{
    unref(e);
    app_select_all(app);
}

static void i_OnZoomIn(App *app, Event *e)
{
    unref(e);
    wave_zoom(app, 1.0 / 1.4);
}

static void i_OnZoomOut(App *app, Event *e)
{
    unref(e);
    wave_zoom(app, 1.4);
}

/* --------------------------------------------------------------- guardado */

/* Guarda el tramo seleccionado de las pistas NO silenciadas. Silenciar una
   pista es exactamente eso: dejarla fuera de lo que se exporta. */
/* ------------------------------------------------------- reproducir lo elegido

   Se exporta el tramo a un fichero temporal con el MISMO camino que usa guardar
   y se reproduce eso. Podria leerse el anillo directamente y ahorrarse el
   fichero, pero entonces habria dos lecturas distintas del mismo audio y nada
   garantizaria que suenan igual: lo que se oye al comprobar dejaria de ser
   prueba de lo que se guarda. */

void app_play_stop(App *app)
{
    if (app->play != NULL)
        ssb_play_close(&app->play);
    if (app->play_file[0] != 0)
    {
        remove(app->play_file);
        app->play_file[0] = 0;
    }
    app_relabel(app);
    view_update(app->view);
}

/* La parte pesada de reproducir: escribe un WAV por pista y los suma. Corre en
   el hilo del trabajo, asi que aqui no se toca ni un control. */
static void i_play_render(App *app, SsbJob *job)
{
    uint32_t i;
    static char_t parts[GUI_MAX_TRACKS][620];
    const char_t *plist[GUI_MAX_TRACKS];
    uint32_t nparts = 0;
    double gain = 1.0;
    char_t uno[620];

    for (i = 0; i < app->ntracks; ++i)
    {
        if (app->tracks[i].muted == TRUE)
            continue;
        bstd_sprintf(parts[nparts], sizeof(parts[0]), "%s-%u.wav", job->base, i + 1);
        if (ssb_track_save_wav(app->tracks[i].track, job->a, job->b, parts[nparts]) != ssb_ok)
            continue;
        plist[nparts] = parts[nparts];
        nparts++;
    }
    if (nparts == 0)
    {
        str_copy_c(job->msg, sizeof(job->msg), T(app, TXT_MSG_OUT_OF_RANGE));
        return;
    }

    bstd_sprintf(uno, sizeof(uno), "%s.wav", job->base);
    {
        ssb_res mr = ssb_mix_wavs(plist, nparts, uno, &gain);
        if (mr != ssb_ok)
        {
            char_t why[200];
            for (i = 0; i < nparts; ++i)
                remove(plist[i]);
            remove(uno);
            bstd_sprintf(why, sizeof(why), T(app, TXT_MSG_MIX_FAILED), ssb_res_str(mr));
            str_copy_c(job->msg, sizeof(job->msg), why);
            return;
        }
    }
    for (i = 0; i < nparts; ++i)
        remove(plist[i]);

    str_copy_c(job->salida, sizeof(job->salida), uno);
}

void app_play_toggle(App *app)
{
    char_t base[620];
    uint32_t i, active = 0;
    ssb_time a, b, wf = 0, wt = 0;

    /* Ya sonando: esto es el boton de pausa. */
    if (app->play != NULL)
    {
        ssb_play_pause(app->play, ssb_play_paused(app->play) ? 0 : 1);
        app_relabel(app);
        view_update(app->view);
        return;
    }

    if (app->has_sel == FALSE)
    {
        i_say(app, T(app, TXT_MSG_NO_SELECTION));
        return;
    }
    for (i = 0; i < app->ntracks; ++i)
        if (app->tracks[i].muted == FALSE)
            active++;
    if (active == 0)
    {
        i_say(app, T(app, TXT_MSG_ALL_MUTED));
        return;
    }

    a = (app->sel_a < app->sel_b) ? app->sel_a : app->sel_b;
    b = (app->sel_a < app->sel_b) ? app->sel_b : app->sel_a;
    if (app_span(app, &wf, &wt) == TRUE)
    {
        if (a < wf)
            a = wf;
        if (b > wt)
            b = wt;
    }
    if (b <= a)
    {
        i_say(app, T(app, TXT_MSG_OUT_OF_RANGE));
        return;
    }

    /* Siempre en un solo fichero, aunque la exportacion este en modo separado:
       reproducir cuatro ficheros a la vez seria otro mezclador, y ya hay uno. */
    bstd_sprintf(base, sizeof(base), "%s/escucha", app->dir);

    /* Se recuerda DONDE empieza lo que va a sonar. La cabeza se dibujara
       respecto a esto, no a la seleccion, que puede cambiar mientras suena. */
    app->play_from = a;
    app->job.a = a;
    app->job.b = b;
    i_job_start(app, base, FALSE, TRUE);
}

/* Exporta el tramo seleccionado.
   `mixed` a TRUE junta todas las pistas activas en un solo fichero. La mezcla
   NO es un segundo camino de exportacion: se escribe cada pista como siempre y
   luego se suman los WAV. Asi el fichero mezclado no puede sonar distinto de lo
   que sale por separado, que es el error facil de cometer aqui. */
/* Deja el resultado donde toque: en el trabajo si viene de un hilo, o en la
   propia interfaz si se llamo desde ella. */
static void i_report(App *app, SsbJob *job, const char_t *text)
{
    if (job != NULL)
        str_copy_c(job->msg, sizeof(job->msg), text);
    else
        i_say(app, text);
}

static void i_save_all(App *app, const char_t *base, int mixed, SsbJob *job)
{
    ssb_time a, b, wf = 0, wt = 0;
    uint32_t i, saved = 0, active = 0;
    char_t msg[260];

    a = (app->sel_a < app->sel_b) ? app->sel_a : app->sel_b;
    b = (app->sel_a < app->sel_b) ? app->sel_b : app->sel_a;

    /* Se recorta contra lo que hay grabado, que ahora es la UNION de las
       pistas. Que todas salgan con la misma duracion lo garantiza la
       exportacion rellenando con silencio, no recortando la seleccion. */
    if (app_span(app, &wf, &wt) == TRUE)
    {
        if (a < wf)
            a = wf;
        if (b > wt)
            b = wt;
    }
    if (b <= a)
    {
        i_report(app, job, T(app, TXT_MSG_OUT_OF_RANGE));
        return;
    }

    for (i = 0; i < app->ntracks; ++i)
        if (app->tracks[i].muted == FALSE)
            active++;
    if (active == 0)
    {
        i_report(app, job, T(app, TXT_MSG_ALL_MUTED));
        return;
    }

    {
        uint32_t sel = popup_get_selected(app->export_fmt);
        ssb_format fmt = (sel == 1) ? ssb_fmt_mp3 : ((sel == 2) ? ssb_fmt_m4a : ssb_fmt_wav);
        uint32_t kbps = (fmt == ssb_fmt_mp3) ? 192u : 128u;
        int fell_back = FALSE;

        static char_t parts[GUI_MAX_TRACKS][620];
        const char_t *plist[GUI_MAX_TRACKS];
        uint32_t nparts = 0;

        for (i = 0; i < app->ntracks; ++i)
        {
            char_t wav[620], out[620];
            if (app->tracks[i].muted == TRUE)
                continue;
            bstd_sprintf(wav, sizeof(wav), "%s-%u.wav", base, i + 1);
            {
                ssb_res wr = ssb_track_save_wav(app->tracks[i].track, a, b, wav);
                if (wr != ssb_ok)
                    continue;
            }
            if (mixed == TRUE)
            {
                /* Se apunta y se sigue: la mezcla necesita TODAS las pistas
                   escritas antes de poder sumar. */
                str_copy_c(parts[nparts], sizeof(parts[0]), wav);
                plist[nparts] = parts[nparts];
                nparts++;
                continue;
            }
            if (fmt == ssb_fmt_wav)
            {
                saved++;
                continue;
            }
            /* Se codifica desde el WAV ya escrito: asi el camino de exportacion
               (ventana comun, huecos, recorte, silenciadas) sigue siendo uno. */
            bstd_sprintf(out, sizeof(out), "%s-%u.%s", base, i + 1, ssb_format_ext(fmt));
            if (ssb_encode(wav, out, fmt, kbps) == ssb_ok)
            {
                remove(wav);
                saved++;
            }
            else
            {
                fell_back = TRUE; /* se queda el WAV, que es mejor que nada */
                saved++;
            }
        }
        if (mixed == TRUE)
        {
            char_t one[620];
            double gain = 1.0;
            uint32_t k;
            if (nparts == 0)
            {
                i_report(app, job, T(app, TXT_MSG_OUT_OF_RANGE));
                return;
            }
            bstd_sprintf(one, sizeof(one), "%s.wav", base);
            {
                ssb_res mr = ssb_mix_wavs(plist, nparts, one, &gain);
                if (mr != ssb_ok)
                {
                    /* Los trozos por pista SE QUEDAN: son exportaciones validas
                       y es mejor darlas que no dar nada. Antes se quedaban
                       tambien, pero sin decirlo, y el usuario veia varios
                       ficheros justo despues de pedir uno solo. */
                    remove(one);
                    if (mr == ssb_err_format)
                    {
                        bstd_sprintf(msg, sizeof(msg), T(app, TXT_MSG_MIX_RATE),
                                     nparts, base, "wav");
                    }
                    else
                    {
                        bstd_sprintf(msg, sizeof(msg), T(app, TXT_MSG_MIX_FAILED),
                                     ssb_res_str(mr));
                    }
                    i_report(app, job, msg);
                    return;
                }
            }
            for (k = 0; k < nparts; ++k)
                remove(plist[k]);
            saved = nparts;
            if (fmt != ssb_fmt_wav)
            {
                char_t enc[620];
                bstd_sprintf(enc, sizeof(enc), "%s.%s", base, ssb_format_ext(fmt));
                if (ssb_encode(one, enc, fmt, kbps) == ssb_ok)
                    remove(one);
                else
                    fell_back = TRUE;
            }
            bstd_sprintf(msg, sizeof(msg), T(app, TXT_MSG_MIXED),
                         nparts, ssb_time_to_sec(b - a), base,
                         fell_back ? "wav" : ssb_format_ext(fmt), gain);
            i_report(app, job, msg);
            return;
        }
        if (fell_back == TRUE)
        {
            bstd_sprintf(msg, sizeof(msg), T(app, TXT_MSG_ENCODE_FAILED), ssb_format_ext(fmt));
            i_report(app, job, msg);
            return;
        }
        bstd_sprintf(msg, sizeof(msg), T(app, TXT_MSG_SAVED),
                     saved, active, ssb_time_to_sec(b - a), base, ssb_format_ext(fmt));
        i_report(app, job, msg);
    }
}

/* ------------------------------------------------- exportar en un hilo

   La regla: el hilo NO toca la interfaz. Escribe ficheros y deja el resultado en
   el trabajo; `i_update` lo recoge y ya en el hilo de interfaz muestra el
   mensaje y, si tocaba, abre la reproduccion. */

int app_busy(const App *app)
{
    int v;
    if (app == NULL || app->job.mtx == NULL)
        return 0;
    bmutex_lock(app->job.mtx);
    v = app->job.activo;
    bmutex_unlock(app->job.mtx);
    return v;
}

static uint32_t i_job_main(App *app)
{
    SsbJob *job = &app->job;

    if (job->para_oir != 0)
        i_play_render(app, job);
    else
        i_save_all(app, job->base, job->mixed, job);

    bmutex_lock(job->mtx);
    job->activo = 0;
    job->hecho = 1;
    bmutex_unlock(job->mtx);
    return 0;
}

/* Arranca el trabajo. Devuelve FALSE si ya habia uno. */
static bool_t i_job_start(App *app, const char_t *base, int mixed, int para_oir)
{
    SsbJob *job = &app->job;

    if (app_busy(app) != 0)
    {
        i_say(app, T(app, TXT_MSG_BUSY));
        return FALSE;
    }
    if (job->th != NULL)
        bthread_close(&job->th);

    str_copy_c(job->base, sizeof(job->base), base);
    job->mixed = mixed;
    job->para_oir = para_oir;
    job->msg[0] = 0;
    job->salida[0] = 0;
    job->hecho = 0;
    job->activo = 1;

    job->th = bthread_create(i_job_main, app, App);
    if (job->th == NULL)
    {
        job->activo = 0;
        i_say(app, T(app, TXT_MSG_PLAY_FAILED));
        return FALSE;
    }
    /* Que se note que esta trabajando: si no, un guardado de varios segundos
       parece que no ha hecho nada. */
    i_say(app, T(app, TXT_MSG_WORKING));
    app_relabel(app);
    return TRUE;
}

/* Recoge el resultado. Se llama desde `i_update`, o sea en el hilo de interfaz. */
static void i_job_collect(App *app)
{
    SsbJob *job = &app->job;
    int hecho;
    char_t msg[260];
    char_t salida[620];
    int para_oir;

    if (job->mtx == NULL)
        return;

    bmutex_lock(job->mtx);
    hecho = job->hecho;
    job->hecho = 0;
    str_copy_c(msg, sizeof(msg), job->msg);
    str_copy_c(salida, sizeof(salida), job->salida);
    para_oir = job->para_oir;
    bmutex_unlock(job->mtx);

    if (hecho == 0)
        return;

    if (para_oir != 0 && salida[0] != 0)
    {
        str_copy_c(app->play_file, sizeof(app->play_file), salida);
        if (ssb_play_open(app->play_file, &app->play) != ssb_ok)
        {
            remove(app->play_file);
            app->play_file[0] = 0;
            i_say(app, T(app, TXT_MSG_PLAY_FAILED));
        }
    }
    if (msg[0] != 0)
        i_say(app, msg);
    app_relabel(app);
    view_update(app->view);
}

static bool_t i_have_selection(App *app)
{
    if (app->has_sel == FALSE || app->ntracks == 0)
    {
        i_say(app, T(app, TXT_MSG_NO_SELECTION));
        return FALSE;
    }
    return TRUE;
}

static void i_stamp(App *app, char_t *out, uint32_t size);

static void i_OnSave(App *app, Event *e)
{
    uint32_t xsel = popup_get_selected(app->export_fmt);
    ssb_format xfmt = (xsel == 1) ? ssb_fmt_mp3 : ((xsel == 2) ? ssb_fmt_m4a : ssb_fmt_wav);
    const char_t *ftypes[1];
    const char_t *path;
    char_t base[600], stamp[40];
    const char_t *dot;
    unref(e);

    if (i_have_selection(app) == FALSE)
        return;
    ftypes[0] = ssb_format_ext(xfmt);
    i_stamp(app, stamp, sizeof(stamp));
    {
        /* El dialogo cambia el directorio de trabajo del proceso. Se restaura
           en cuanto vuelve, para que nada relativo se quede apuntando a otro
           sitio. Las rutas propias ya son absolutas, pero esto lo cierra. */
        char_t cwd[600];
        ssb_abs_path(".", cwd, sizeof(cwd));
        path = comwin_save_file(app->window, T(app, TXT_SAVE_DIALOG), ftypes, 1, app->savedir, stamp);
        ssb_set_cwd(cwd);
    }
    if (path == NULL)
        return;

    /* El usuario pudo navegar a otra carpeta dentro del propio dialogo. Esa es
       su eleccion tanto como pulsar el boton de carpeta, asi que se queda: si
       no, el siguiente dialogo volveria a abrirse donde ya no quiere guardar. */
    {
        String *dirpart = NULL;
        String *filepart = NULL;
        str_split_pathname(path, &dirpart, &filepart);
        if (dirpart != NULL)
        {
            if (str_empty(dirpart) == FALSE)
            {
                str_copy_c(app->savedir, sizeof(app->savedir), tc(dirpart));
                app_save_settings(app);
            }
            str_destroy(&dirpart);
        }
        if (filepart != NULL)
            str_destroy(&filepart);
    }

    {
        char_t suffix[8];
        bstd_sprintf(suffix, sizeof(suffix), ".%s", ssb_format_ext(xfmt));
        dot = str_str(path, suffix);
    }
    if (dot != NULL)
        str_copy_cn(base, sizeof(base), path, (uint32_t)(dot - path));
    else
        str_copy_c(base, sizeof(base), path);
    base[sizeof(base) - 1] = 0;
    i_job_start(app, base, app->mix_export, FALSE);
}

/* Nombre por omision: la fecha y hora reales del principio del tramo
   seleccionado. Un fichero llamado "selection-3" no dice nada dentro de un mes. */
static void i_stamp(App *app, char_t *out, uint32_t size)
{
    ssb_time a = (app->sel_a < app->sel_b) ? app->sel_a : app->sel_b;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    ssb_wall_clock(a, &y, &mo, &d, &h, &mi, &sec);
    bstd_sprintf(out, size, "%04d-%02d-%02d_%02d-%02d-%02d", y, mo, d, h, mi, sec);
}

/* Guardado rapido con Ctrl+S: sin dialogo, a la carpeta elegida. */
void app_quick_save(App *app)
{
    char_t base[600], stamp[40];
    if (i_have_selection(app) == FALSE)
        return;
    i_stamp(app, stamp, sizeof(stamp));
    bstd_sprintf(base, sizeof(base), "%s/%s", app->savedir, stamp);
    i_job_start(app, base, app->mix_export, FALSE);
}

static void i_OnQuickSave(App *app, Event *e)
{
    unref(e);
    app_quick_save(app);
}

/* Modo reducido: se oculta la segunda fila de la barra y el lienzo dibuja
   bandas bajas, sin regla. Lo minimo para grabar y ver que entra senal. */
void app_set_small(App *app, int compact)
{
    app->compact = compact ? TRUE : FALSE;
    if (app->bar != NULL)
    {
        cell_visible(layout_cell(app->bar, 0, 1), app->compact ? FALSE : TRUE);
        layout_update(app->bar);
    }
    button_text(app->btn_small, T(app, app->compact ? TXT_MODE_FULL : TXT_MODE_SMALL));
    wave_resize(app);
    view_update(app->view);
}

/* Modo comandos: aparece la linea de entrada y la consola en el lienzo. */
void app_set_cmdmode(App *app, int on)
{
    app->cmdmode = on ? TRUE : FALSE;
    if (app->root != NULL)
    {
        cell_visible(layout_cell(app->root, 0, 2), app->cmdmode ? TRUE : FALSE);
        layout_update(app->root);
    }
    if (app->cmdmode == TRUE)
    {
        cmd_print(app, "%s", T(app, TXT_CMD_HELLO));
        window_focus(app->window, guicontrol(app->cmd));
    }
    button_text(app->btn_cmd, T(app, app->cmdmode ? TXT_MODE_NOCMD : TXT_MODE_CMD));
    view_update(app->view);
}

static void i_OnSmall(App *app, Event *e)
{
    unref(e);
    app_set_small(app, app->compact ? FALSE : TRUE);
}

static void i_OnCmdMode(App *app, Event *e)
{
    unref(e);
    app_set_cmdmode(app, app->cmdmode ? FALSE : TRUE);
}

/* `edit_OnChange` de NAppGUI solo salta al PERDER EL FOCO, no con Enter (ver
   `_osedit_resign_focus` en osgui/win/osedit.c). Para que Enter ejecute, se
   registra Return como atajo de ventana y se lee el texto del control. */
static void i_OnCmdEnter(App *app, Event *e)
{
    char_t line[200];
    unref(e);
    if (app->cmdmode == FALSE || app->cmd == NULL)
        return;
    str_copy_c(line, sizeof(line), edit_get_text(app->cmd));
    if (line[0] == 0)
        return;
    edit_text(app->cmd, "");
    cmd_run(app, line);
}

/* ------------------------------------------------------------------ idioma */

void app_relabel(App *app)
{
    uint32_t i;
    button_text(app->btn_add, T(app, TXT_ADD));
    button_text(app->btn_input, T(app, app->recording ? TXT_STOP_REC : TXT_REC));
    button_text(app->btn_pause, T(app, app->frozen ? TXT_RESUME_VIEW : TXT_FREEZE_VIEW));
    button_text(app->btn_live, T(app, TXT_LIVE));
    button_text(app->btn_all, T(app, TXT_SELECT_ALL));
    button_text(app->btn_save, T(app, TXT_SAVE));
    button_text(app->btn_lang, T(app, TXT_LANG_BUTTON));
    button_text(app->btn_folder, T(app, TXT_FOLDER));
    label_text(app->lbl_buffer, T(app, TXT_BUFFER));
    label_text(app->lbl_save, T(app, TXT_FORMAT));
    label_text(app->lbl_export, T(app, TXT_EXPORT));
    button_tooltip(app->btn_input, T(app, TXT_TIP_STOP_INPUT));
    button_tooltip(app->btn_pause, T(app, TXT_TIP_FREEZE_VIEW));
    button_tooltip(app->btn_lang, T(app, TXT_TIP_LANG));
    button_tooltip(app->btn_folder, T(app, TXT_TIP_FOLDER));
    button_text(app->btn_folder_def, T(app, TXT_FOLDER_RESET));
    /* El boton dice lo que HARA si lo pulsas, no en que estado esta. */
    if (app->play == NULL)
    {
        button_text(app->btn_play, T(app, TXT_PLAY));
        button_image(app->btn_play, app->ico_play);
    }
    else if (ssb_play_paused(app->play) == 1)
    {
        button_text(app->btn_play, T(app, TXT_RESUME_PLAY));
        button_image(app->btn_play, app->ico_play);
    }
    else
    {
        button_text(app->btn_play, T(app, TXT_PAUSE));
        button_image(app->btn_play, app->ico_pause);
    }
    button_text(app->btn_mix, T(app, app->mix_export ? TXT_MIX_ON : TXT_MIX_OFF));
    popup_tooltip(app->buffer, T(app, TXT_TIP_BUFFER));
    popup_tooltip(app->compress, T(app, TXT_TIP_FORMAT));
    popup_tooltip(app->export_fmt, T(app, TXT_TIP_EXPORT));

    {
        uint32_t sel = popup_get_selected(app->compress);
        popup_set_elem(app->compress, 0, T(app, TXT_LOSSLESS), NULL);
        popup_set_elem(app->compress, 1, T(app, TXT_UNCOMPRESSED), NULL);
        popup_set_elem(app->compress, 2, T(app, TXT_LOSSLESS24), NULL);
        popup_set_elem(app->compress, 3, T(app, TXT_UNCOMPRESSED24), NULL);
        popup_selected(app->compress, sel);
    }
    for (i = 0; i < app->ntracks; ++i)
        bstd_sprintf(app->tracks[i].name, sizeof(app->tracks[i].name), "%s %u",
                     app->lang == LANG_EN ? "Track" : "Pista", i + 1);
    app_reload_sources(app);
    window_title(app->window, T(app, TXT_TITLE));
    /* Los textos cambian de ancho: hay que recomponer o quedan recortados. */
    if (app->root != NULL)
        layout_update(app->root);
    view_update(app->view);
}

static void i_OnLang(App *app, Event *e)
{
    unref(e);
    app->lang = (app->lang + 1) % LANG_COUNT;
    app_relabel(app);
}

/* ------------------------------------------------------------------ ventana */

/* Dos filas: en una sola no cabe sin recortar los ultimos botones. */
/* La raya separadora.
 *
 * El View se pide de 3 px de ancho y la raya se dibuja DENTRO, de uno solo.
 * Pedir un control de 1 px no sirve: ni un View ni una etiqueta bajan de unos
 * 3, y la raya salia como un bloque. Dibujandola dentro, el ancho lo decide el
 * dibujo y no el control.
 *
 * Va recogida 2 px por arriba y por abajo: una raya que llega a los bordes
 * parece un trozo de marco; recogida se lee como lo que es, una separacion. */
static void i_OnSepDraw(App *app, Event *e)
{
    const EvDraw *p = event_params(e, EvDraw);
    draw_fill_color(p->ctx, app->pal.chrome);
    draw_rect(p->ctx, ekFILL, 0, 0, p->width, p->height);
    draw_fill_color(p->ctx, app->pal.sep);
    draw_rect(p->ctx, ekFILL, (real32_t)(int32_t)(p->width * .5f), 2, 1, p->height - 4);
}

/* Los dos botones con icono llevan una holgura EXPLICITA.
 *
 * La que pone NAppGUI por omision a un boton plano con icono es medio icono
 * —aqui 9 px— y no llega: el texto salia cortado. En la captura de portada se
 * leia "Recorc". Medido subiendo la holgura: con 12 ya entra entero, asi que 20
 * deja margen de sobra y ademas lo deja con el aire de un boton de barra.
 *
 * La holgura por omision se calcula solo con el icono, sin mirar el texto, asi
 * que un icono pequeno con un rotulo largo siempre va justo. Aqui hay uno de
 * 18 px al lado de "Reproducir". */
#define BTN_HPAD 20.f

/* Ancho de un boton que CAMBIA de rotulo.
 *
 * Necesitan ancho fijo: sin el, la barra entera se recoloca al pulsarlos y los
 * de al lado se mueven bajo el raton justo cuando vas a pulsarlos. Pero el
 * numero no se pone a ojo — se puso, 96 mirando "Record", y en espanol
 * "Reproducir" no cabia. Se mide el rotulo mas largo de los que ese boton va a
 * llevar, con la misma cuenta que hace el boton plano. */
static real32_t i_btn_width(const App *app, const ssb_txt *ids, uint32_t n, const Image *icon)
{
    Font *f = font_system(font_regular_size(), 0);
    real32_t best = 0.f;
    uint32_t i;

    for (i = 0; i < n; ++i)
    {
        real32_t w = 0.f, h = 0.f;
        font_extents(f, T(app, ids[i]), -1.f, &w, &h);
        if (w > best)
            best = w;
    }
    font_destroy(&f);

    if (icon != NULL)
        best += (real32_t)image_width(icon) + 4.f;

    return best + BTN_HPAD;
}

static Layout *i_toolbar(App *app)
{
    Layout *layout = layout_create(1, 2);
    Layout *r0 = layout_create(14, 1);
    Layout *r1 = layout_create(18, 1);

    app->sources = popup_create();
    app->buffer = popup_create();
    app->compress = popup_create();
    app->export_fmt = popup_create();
    /* PLANOS, no `button_push()`. Es el boton de barra de herramientas: sin
       borde, del color del fondo, y solo se resalta al pasar el raton. Lo
       dibuja NAppGUI, asi que se ve igual con el tema claro y con el oscuro.
       Hasta NAP-045 no admitia texto sin icono; ahora si. */
    app->btn_add = button_flat();
    app->btn_input = button_flat();
    app->btn_pause = button_flat();
    app->btn_live = button_flat();
    app->btn_all = button_flat();
    app->btn_save = button_flat();
    app->btn_lang = button_flat();
    app->btn_zin = button_flat();
    app->btn_zout = button_flat();
    app->btn_folder = button_flat();
    app->btn_folder_def = button_flat();
    app->btn_small = button_flat();
    app->btn_cmd = button_flat();
    app->btn_play = button_flat();
    app->btn_mix = button_flat();
    {
        uint32_t si;
        for (si = 0; si < SEP_N; ++si)
        {
            app->sep[si] = view_create();
            view_size(app->sep[si], s2df(3, 20));
            view_OnDraw(app->sep[si], listener(app, i_OnSepDraw, App));
        }
    }
    app->lbl_buffer = label_create();
    app->lbl_save = label_create();
    app->lbl_export = label_create();

    /* Los textos van AQUI, no en app_relabel: el layout calcula el ancho de
       cada columna con el contenido que hay en ese momento, y si los botones
       estan vacios salen todos del mismo tamano y recortan el texto luego. */
    button_text(app->btn_add, T(app, TXT_ADD));
    button_text(app->btn_input, T(app, TXT_REC));
    /* La imagen tambien AQUI, no solo en app_relabel: el tamano del boton se
       calcula al componer el layout, y si en ese momento no hay imagen no se
       reserva sitio para ella. Ponerla despues la dejaba invisible hasta que
       algo forzara un recalculo â€” el circulo rojo no salia hasta que pulsabas
       Grabar una vez. */
    button_image(app->btn_input, app->ico_rec);
    button_image(app->btn_play, app->ico_play);
    /* Un boton plano usa el texto como etiqueta emergente del icono: para que
       se dibujen LOS DOS hay que decir donde va el icono. */
    button_image_pos(app->btn_input, ekGUI_POS_LEFT);
    button_hpadding(app->btn_input, BTN_HPAD);
    button_hpadding(app->btn_play, BTN_HPAD);
    button_image_pos(app->btn_play, ekGUI_POS_LEFT);
    button_text(app->btn_pause, T(app, TXT_FREEZE_VIEW));
    button_text(app->btn_live, T(app, TXT_LIVE));
    button_text(app->btn_all, T(app, TXT_SELECT_ALL));
    button_text(app->btn_save, T(app, TXT_SAVE));
    button_text(app->btn_lang, T(app, TXT_LANG_BUTTON));
    button_text(app->btn_folder, T(app, TXT_FOLDER));
    label_text(app->lbl_buffer, T(app, TXT_BUFFER));
    label_text(app->lbl_save, T(app, TXT_FORMAT));
    label_text(app->lbl_export, T(app, TXT_EXPORT));
    button_text(app->btn_folder, T(app, TXT_FOLDER));
    button_text(app->btn_folder_def, T(app, TXT_FOLDER_RESET));
    button_text(app->btn_small, T(app, TXT_MODE_SMALL));
    button_text(app->btn_cmd, T(app, TXT_MODE_CMD));
    button_text(app->btn_zin, T(app, TXT_ZOOM_IN));
    button_text(app->btn_zout, T(app, TXT_ZOOM_OUT));
    /* El ancho minimo va por `layout_hsize`, no por `button_width`: ese solo lo
       mira el boton normal, y estos ya son planos. Hace falta en los que CAMBIAN
       de rotulo (Grabar/Parar, Reproducir/Pausa, un fichero/aparte): sin ancho
       fijo la barra entera se recoloca al pulsarlos y los botones de al lado se
       mueven bajo el raton justo cuando vas a pulsarlos. */

    {
        uint32_t bi;
        for (bi = 0; bi < BUFFER_N; ++bi)
            popup_add_elem(app->buffer, i_BUFFER_TEXT[bi], NULL);
    }
    popup_selected(app->buffer, 4); /* 5 min */
    popup_OnSelect(app->buffer, listener(app, i_OnBuffer, App));
    popup_add_elem(app->compress, T(app, TXT_LOSSLESS), NULL);
    popup_add_elem(app->compress, T(app, TXT_UNCOMPRESSED), NULL);
    popup_add_elem(app->compress, T(app, TXT_LOSSLESS24), NULL);
    popup_add_elem(app->compress, T(app, TXT_UNCOMPRESSED24), NULL);
    popup_selected(app->compress, 0);
    popup_add_elem(app->export_fmt, "WAV", NULL);
    popup_add_elem(app->export_fmt, "MP3", NULL);
    popup_add_elem(app->export_fmt, "M4A (AAC)", NULL);
    popup_selected(app->export_fmt, 0);
    popup_list_height(app->sources, 12);

    button_OnClick(app->btn_add, listener(app, i_OnAdd, App));
    button_OnClick(app->btn_input, listener(app, i_OnInput, App));
    button_OnClick(app->btn_pause, listener(app, i_OnPause, App));
    button_OnClick(app->btn_live, listener(app, i_OnLive, App));
    button_OnClick(app->btn_all, listener(app, i_OnSelectAll, App));
    button_OnClick(app->btn_save, listener(app, i_OnSave, App));
    button_OnClick(app->btn_lang, listener(app, i_OnLang, App));
    button_OnClick(app->btn_folder, listener(app, i_OnFolder, App));
    button_OnClick(app->btn_folder_def, listener(app, i_OnFolderDefault, App));
    button_OnClick(app->btn_small, listener(app, i_OnSmall, App));
    button_OnClick(app->btn_play, listener(app, i_OnPlay, App));
    button_OnClick(app->btn_mix, listener(app, i_OnMix, App));
    button_OnClick(app->btn_cmd, listener(app, i_OnCmdMode, App));
    button_OnClick(app->btn_zin, listener(app, i_OnZoomIn, App));
    button_OnClick(app->btn_zout, listener(app, i_OnZoomOut, App));

    /* Dos filas de grupos, y entre grupo y grupo una RAYA FINA.
     *
     * Antes los grupos se marcaban solo con la separacion, y funcionaba porque
     * cada boton tenia su marco: el ojo veia bloques de botones. Con botones
     * planos, que se funden con el fondo, ya no hay bloques que ver y la barra
     * se lee como una fila larga de palabras sueltas. La raya devuelve la
     * lectura por grupos gastando un pixel.
     *
     * Dentro de un grupo, 4 px. A cada lado de una raya, 10.
     *
     * Fila 0 â€” QUE se graba y COMO se maneja:
     *   [fuente] [Anadir] | [Grabar] [Reproducir] .. | [carpeta] | [modos] | [idioma] */
    layout_popup(r0, app->sources, 0, 0);
    layout_button(r0, app->btn_add, 1, 0);
    layout_view(r0, app->sep[0], 2, 0);
    layout_button(r0, app->btn_input, 3, 0);
    layout_button(r0, app->btn_play, 4, 0);
    /* El espaciador tiene que ser una etiqueta VACIA, no un boton: poner el
       ensanchado sobre la columna de un boton lo estira a el y deja el resto
       apretado. `Select all` quedaba en una astilla. */
    layout_label(r0, label_create(), 5, 0);
    layout_view(r0, app->sep[1], 6, 0);
    layout_button(r0, app->btn_folder, 7, 0);
    layout_button(r0, app->btn_folder_def, 8, 0);
    layout_view(r0, app->sep[2], 9, 0);
    layout_button(r0, app->btn_small, 10, 0);
    layout_button(r0, app->btn_cmd, 11, 0);
    layout_view(r0, app->sep[3], 12, 0);
    layout_button(r0, app->btn_lang, 13, 0);
    layout_hsize(r0, 0, 240);
    {
        /* Los rotulos que puede llegar a llevar cada uno, no solo el de ahora. */
        ssb_txt rec[2];
        ssb_txt play[3];
        rec[0] = TXT_REC;
        rec[1] = TXT_STOP_REC;
        play[0] = TXT_PLAY;
        play[1] = TXT_PAUSE;
        play[2] = TXT_RESUME_PLAY;
        layout_hsize(r0, 3, i_btn_width(app, rec, 2, app->ico_rec));
        layout_hsize(r0, 4, i_btn_width(app, play, 3, app->ico_play));
    }
    layout_hsize(r0, 13, 40);
    layout_hexpand(r0, 5);
    layout_hmargin(r0, 0, 4);
    layout_hmargin(r0, 1, 10);
    layout_hmargin(r0, 2, 10);
    layout_hmargin(r0, 3, 4);
    layout_hmargin(r0, 5, 10);
    layout_hmargin(r0, 6, 10);
    layout_hmargin(r0, 7, 4);
    layout_hmargin(r0, 8, 10);
    layout_hmargin(r0, 9, 10);
    layout_hmargin(r0, 10, 4);
    layout_hmargin(r0, 11, 10);
    layout_hmargin(r0, 12, 10);

    /* Fila 1 â€” que se guarda y como se mira:
         Buffer y calidad | zoom | vista .. | formato | guardado */
    layout_label(r1, app->lbl_buffer, 0, 0);
    layout_popup(r1, app->buffer, 1, 0);
    layout_label(r1, app->lbl_save, 2, 0);
    layout_popup(r1, app->compress, 3, 0);
    layout_view(r1, app->sep[4], 4, 0);
    layout_button(r1, app->btn_zout, 5, 0);
    layout_button(r1, app->btn_zin, 6, 0);
    layout_view(r1, app->sep[5], 7, 0);
    layout_button(r1, app->btn_pause, 8, 0);
    layout_button(r1, app->btn_live, 9, 0);
    layout_button(r1, app->btn_all, 10, 0);
    layout_label(r1, label_create(), 11, 0);
    layout_view(r1, app->sep[6], 12, 0);
    layout_label(r1, app->lbl_export, 13, 0);
    layout_popup(r1, app->export_fmt, 14, 0);
    layout_view(r1, app->sep[7], 15, 0);
    layout_button(r1, app->btn_mix, 16, 0);
    layout_button(r1, app->btn_save, 17, 0);
    layout_hsize(r1, 5, 34);
    layout_hsize(r1, 6, 34);
    {
        ssb_txt mix[2];
        mix[0] = TXT_MIX_ON;
        mix[1] = TXT_MIX_OFF;
        button_hpadding(app->btn_mix, BTN_HPAD);
        layout_hsize(r1, 16, i_btn_width(app, mix, 2, NULL));
    }
    layout_hexpand(r1, 11);
    layout_hmargin(r1, 0, 4);
    layout_hmargin(r1, 1, 14);
    layout_hmargin(r1, 2, 4);
    layout_hmargin(r1, 3, 10);
    layout_hmargin(r1, 4, 10);
    layout_hmargin(r1, 5, 3);
    layout_hmargin(r1, 6, 10);
    layout_hmargin(r1, 7, 10);
    layout_hmargin(r1, 8, 4);
    layout_hmargin(r1, 9, 4);
    layout_hmargin(r1, 11, 10);
    layout_hmargin(r1, 12, 10);
    layout_hmargin(r1, 13, 4);
    layout_hmargin(r1, 14, 10);
    layout_hmargin(r1, 15, 10);
    layout_hmargin(r1, 16, 4);

    /* Las rayas: 1 px de ancho y estiradas a lo alto de la fila. Sin el
       estirado, una etiqueta mide lo que su texto â€” nada â€” y no se ve. */
    {
        uint32_t c0[4] = {2, 6, 9, 12};
        uint32_t c1[4] = {4, 7, 12, 15};
        uint32_t si;
        for (si = 0; si < 4; ++si)
        {
            layout_hsize(r0, c0[si], 3);
            layout_valign(r0, c0[si], 0, ekVJUSTIFY);
            layout_hsize(r1, c1[si], 3);
            layout_valign(r1, c1[si], 0, ekVJUSTIFY);
        }
    }

    app->bar0 = r0;
    app->bar1 = r1;
    app->bar = layout;
    layout_layout(layout, r0, 0, 0);
    layout_layout(layout, r1, 0, 1);
    layout_vmargin(layout, 0, 6);
    return layout;
}

static Panel *i_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *layout = layout_create(1, 3);
    Layout *bar;

    app->view = wave_create(app);
    bar = i_toolbar(app);
    app->cmd = edit_create();
    edit_phtext(app->cmd, "rec | stop | add output | save 30 | help");
    app->root = layout;

    layout_layout(layout, bar, 0, 0);
    layout_view(layout, app->view, 0, 1);
    layout_edit(layout, app->cmd, 0, 2);
    layout_vexpand(layout, 1);
    layout_vmargin(layout, 0, 8);
    layout_vmargin(layout, 1, 6);
    layout_margin(layout, 8);
    panel_layout(panel, layout);
    return panel;
}

/* NAppGUI no tematiza los controles nativos de Windows: los botones y los
   desplegables los pinta el sistema. Lo que si esta en nuestra mano es el fondo
   de los layouts, el color de las etiquetas y la barra de titulo. Ver la ficha
   NAP-042 del backlog de nappgui. */
void app_apply_chrome(App *app)
{
    if (app->root != NULL)
        layout_bgcolor(app->root, app->pal.chrome);
    if (app->bar0 != NULL)
        layout_bgcolor(app->bar0, app->pal.chrome);
    if (app->bar1 != NULL)
        layout_bgcolor(app->bar1, app->pal.chrome);
    if (app->lbl_buffer != NULL)
    {
        label_color(app->lbl_buffer, app->pal.chrome_tx);
        label_bgcolor(app->lbl_buffer, app->pal.chrome);
    }
    if (app->lbl_save != NULL)
    {
        label_color(app->lbl_save, app->pal.chrome_tx);
        label_bgcolor(app->lbl_save, app->pal.chrome);
    }
    if (app->lbl_export != NULL)
    {
        label_color(app->lbl_export, app->pal.chrome_tx);
        label_bgcolor(app->lbl_export, app->pal.chrome);
    }
    {
        uint32_t si;
        for (si = 0; si < SEP_N; ++si)
        {
            if (app->sep[si] != NULL)
                view_update(app->sep[si]);
        }
    }
    if (app->window != NULL)
        window_update(app->window);
}

static void i_OnTheme(App *app, Event *e)
{
    uint32_t i;
    unref(e);
    wave_palette(app);
    app_apply_chrome(app);
    for (i = 0; i < app->ntracks; ++i)
        app->tracks[i].color = wave_track_color(app, i);
    view_update(app->view);
}

static void i_OnClose(App *app, Event *e)
{
    app_play_stop(app);
    app_save_settings(app);
    unref(e);
    osapp_finish();
}

/* Ajustes que sobreviven a cerrar el programa.
 *
 * Van a %APPDATA%\SystemSoundBuffer\ssb.cfg, no junto al ejecutable ni junto
 * al buffer: si dependieran del directorio de trabajo se perderian en cuanto la
 * aplicacion se lanzase desde otro sitio, que es justo lo que hace un acceso
 * directo. Formato de texto plano, una `clave=valor` por linea, para poder
 * arreglarlo a mano si algo se tuerce.
 *
 * Lo que NO se guarda es tan deliberado como lo que si: las pistas y su estado
 * de grabacion no se restauran. Que un programa que graba audio arranque solo
 * capturando lo que oye seria una sorpresa desagradable. */
#define SSB_CFG_APP  "SystemSoundBuffer"
#define SSB_CFG_FILE "ssb.cfg"

void app_save_settings(App *app)
{
    char path[700];
    FILE *f;

    if (ssb_config_path(SSB_CFG_APP, SSB_CFG_FILE, path, sizeof(path)) != ssb_ok)
        return;
    f = fopen(path, "wb");
    if (f == NULL)
        return;
    fprintf(f, "savedir=%s\n", app->savedir);
    fprintf(f, "lang=%u\n", app->lang);
    fprintf(f, "theme=%u\n", app->theme);
    /* Los segundos de verdad, no el indice del desplegable: la orden `buffer`
       admite valores que no son ninguno de los preajustes. */
    fprintf(f, "buffer_secs=%u\n", (uint32_t)app->buffer_secs);
    fprintf(f, "compress=%u\n", popup_get_selected(app->compress));
    fprintf(f, "export=%u\n", popup_get_selected(app->export_fmt));
    fclose(f);
}

static void i_settings_load(App *app)
{
    char path[700];
    char line[700];
    FILE *f;

    if (ssb_config_path(SSB_CFG_APP, SSB_CFG_FILE, path, sizeof(path)) != ssb_ok)
        return;
    f = fopen(path, "rb");
    if (f == NULL)
        return;
    while (fgets(line, (int)sizeof(line), f) != NULL)
    {
        char *eq = strchr(line, '=');
        char *val;
        uint32_t n;
        if (eq == NULL)
            continue;
        *eq = 0;
        val = eq + 1;
        {
            /* fgets se trae el salto de linea; fuera, que si no acaba dentro
               de la ruta y el directorio deja de existir. */
            size_t len = strlen(val);
            while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r'))
                val[--len] = 0;
        }
        n = (uint32_t)atoi(val);
        if (strcmp(line, "savedir") == 0)
        {
            /* Solo si sigue existiendo: una carpeta en una unidad extraible que
               ya no esta debe caer a la de siempre, no dejar el programa
               guardando en el limbo. */
            if (val[0] != 0 && ssb_mkdir(val) == ssb_ok)
                str_copy_c(app->savedir, sizeof(app->savedir), val);
        }
        else if (strcmp(line, "lang") == 0)
        {
            app->lang = (n < 2) ? n : 0;
        }
        else if (strcmp(line, "theme") == 0)
        {
            app->theme = (n < 3) ? n : 0;
        }
        else if (strcmp(line, "buffer_secs") == 0)
        {
            if (n > 0)
            {
                uint32_t k;
                app->buffer_secs = (double)n;
                app->span_secs = app->buffer_secs;
                for (k = 0; k < BUFFER_N; ++k)
                {
                    if (i_BUFFER_SECS[k] == n)
                        popup_selected(app->buffer, k);
                }
            }
        }
        else if (strcmp(line, "compress") == 0)
        {
            popup_selected(app->compress, (n < 4) ? n : 0);
        }
        else if (strcmp(line, "export") == 0)
        {
            popup_selected(app->export_fmt, (n < 3) ? n : 0);
        }
        else if (strcmp(line, "mix") == 0)
        {
            app->mix_export = (n != 0) ? TRUE : FALSE;
        }
    }
    fclose(f);
    wave_palette(app);
}

/* Argumentos:
     ssbgui --src output --src app:WhatsApp --src input
            --buffer 0..8   --theme system|light|dark   --lang en|es   */
static void i_read_args(App *app)
{
    uint32_t n = osapp_argc();
    uint32_t i;

    for (i = 0; i + 1 < n; ++i)
    {
        char_t arg[64], val[64];
        osapp_argv(i, arg, sizeof(arg));
        osapp_argv(i + 1, val, sizeof(val));
        if (str_equ_c(arg, "--theme") == TRUE)
        {
            if (str_equ_c(val, "light") == TRUE)
                app->theme = 1;
            else if (str_equ_c(val, "dark") == TRUE)
                app->theme = 2;
            else
                app->theme = 0;
            wave_palette(app);
            app_apply_chrome(app);
        }
        else if (str_equ_c(arg, "--lang") == TRUE)
        {
            app->lang = (str_equ_c(val, "es") == TRUE) ? LANG_ES : LANG_EN;
            app_relabel(app);
        }
        else if (str_equ_c(arg, "--script") == TRUE)
        {
            /* La ruta no cabe en `val`, que es corto para las opciones cortas. */
            char_t path[420];
            osapp_argv(i + 1, path, sizeof(path));
            i_script_load(app, path);
        }
        else if (str_equ_c(arg, "--export") == TRUE)
        {
            uint32_t f = 0;
            if (str_equ_c(val, "mp3") == TRUE)
                f = 1;
            else if (str_equ_c(val, "m4a") == TRUE || str_equ_c(val, "aac") == TRUE)
                f = 2;
            popup_selected(app->export_fmt, f);
        }
        else if (str_equ_c(arg, "--mode") == TRUE)
        {
            if (str_equ_c(val, "small") == TRUE)
                app_set_small(app, TRUE);
            else if (str_equ_c(val, "cmd") == TRUE)
                app_set_cmdmode(app, TRUE);
        }
        else if (str_equ_c(arg, "--buffer") == TRUE)
        {
            /* El desplegable y `buffer_secs` tienen que ir juntos: las pistas
               nuevas leen `buffer_secs`, asi que mover solo el desplegable
               dejaba la pista con el valor guardado de la sesion anterior. */
            uint32_t idx = (uint32_t)atoi(val);
            if (idx >= BUFFER_N)
                idx = 4;
            popup_selected(app->buffer, idx);
            app->buffer_secs = (double)i_BUFFER_SECS[idx];
            app->span_secs = app->buffer_secs;
        }
    }
    for (i = 0; i + 1 < n; ++i)
    {
        char_t arg[64];
        osapp_argv(i, arg, sizeof(arg));
        if (str_equ_c(arg, "--src") == TRUE)
        {
            char_t spec[SSB_NAME_MAX];
            ssb_source src;
            osapp_argv(i + 1, spec, sizeof(spec));
            if (ssb_source_parse(spec, &src) == ssb_ok)
                app_add_track(app, &src);
        }
    }
}

static App *i_create(void)
{
    App *app = heap_new0(App);
    Panel *panel;

    app->lang = LANG_EN;
    app->buffer_secs = 300.0;
    app->job.mtx = bmutex_create();
    app->span_secs = app->buffer_secs;
    app->follow = TRUE;
    app->peaks_cap = 250000;
    app->peaks = heap_new_n(app->peaks_cap, ssb_peak);
    app->font = font_system(11, 0);
    icons_create(app);
    ssb_watch_start();
    /* Rutas ABSOLUTAS desde el primer momento. Abrir un dialogo de fichero hace
       que Windows cambie el directorio de trabajo del proceso (NAppGUI no pone
       OFN_NOCHANGEDIR, ver osgui/win/oscomwin.c:217), y a partir de ahi los
       anillos dejaban de encontrar sus propios segmentos: los WAV salian vacios
       y la codificacion a MP3 fallaba al leerlos. */
    ssb_abs_path("ssb-gui-buffer", app->dir, sizeof(app->dir));
    str_copy_c(app->savedir, sizeof(app->savedir), app->dir);
    ssb_mkdir(app->dir);
    wave_palette(app);

    panel = i_panel(app);
    app->window = window_create(ekWINDOW_STDRES);
    window_panel(app->window, panel);
    window_title(app->window, T(app, TXT_TITLE));
    window_OnClose(app->window, listener(app, i_OnClose, App));
    /* Atajos: Ctrl+P detiene o reanuda la captura, Ctrl+E congela la vista. */
    window_hotkey(app->window, ekKEY_S, ekMKEY_CONTROL, listener(app, i_OnQuickSave, App));
    window_hotkey(app->window, ekKEY_P, ekMKEY_CONTROL, listener(app, i_OnInput, App));
    window_hotkey(app->window, ekKEY_E, ekMKEY_CONTROL, listener(app, i_OnPause, App));
    window_hotkey(app->window, ekKEY_A, ekMKEY_CONTROL, listener(app, i_OnSelectAll, App));
    window_hotkey(app->window, ekKEY_M, ekMKEY_CONTROL, listener(app, i_OnSmall, App));
    window_hotkey(app->window, ekKEY_K, ekMKEY_CONTROL, listener(app, i_OnCmdMode, App));
    window_hotkey(app->window, ekKEY_RETURN, 0, listener(app, i_OnCmdEnter, App));
    /* Comandos de teclado. Van con Ctrl a proposito: la consola de comandos es
       un campo de texto, y sin modificador cualquier atajo se comeria lo que se
       esta escribiendo. F1 se queda suelta porque no es una letra. */
    window_hotkey(app->window, ekKEY_R, ekMKEY_CONTROL, listener(app, i_OnInput, App));
    window_hotkey(app->window, ekKEY_L, ekMKEY_CONTROL, listener(app, i_OnPlay, App));
    window_hotkey(app->window, ekKEY_H, ekMKEY_CONTROL, listener(app, i_OnHush, App));
    window_hotkey(app->window, ekKEY_G, ekMKEY_CONTROL, listener(app, i_OnLive, App));
    window_hotkey(app->window, ekKEY_J, ekMKEY_CONTROL, listener(app, i_OnMix, App));
    window_hotkey(app->window, ekKEY_D, ekMKEY_CONTROL, listener(app, i_OnFolder, App));
    window_hotkey(app->window, ekKEY_F1, 0, listener(app, i_OnKeyHelp, App));
    gui_OnThemeChanged(listener(app, i_OnTheme, App));
    /* Todo lo que queda por hacer va ANTES de ensenar la ventana.
     *
     * Estaba despues, y el resultado era el destello: la ventana aparecia y los
     * botones y el lienzo se veian en blanco hasta que estas llamadas
     * terminaban y el bucle de mensajes llegaba a repintar. Enumerar los
     * dispositivos de audio —`app_reload_sources`— es lo que mas tarda, porque
     * abre COM y pregunta al sistema.
     *
     * Medido con BitBlt del DC de la ventana cada 30 ms: 54 % de la ventana en
     * blanco desde los 542 ms hasta los 906 ms. Los controles no es que se
     * pintaran de blanco: es que no habian recibido su primer WM_PAINT, y una
     * ventana hija sin pintar se ve blanca. Se comprobo tinendo el relleno de
     * los botones de rojo y el del lienzo de verde: durante el destello no
     * habia ni rojo ni verde, y los dos aparecian de golpe al terminar. */
    app_apply_chrome(app);
    app_reload_sources(app);
    app_set_cmdmode(app, FALSE);
    i_settings_load(app);
    app_relabel(app);
    app_apply_chrome(app);
    i_read_args(app);
    wave_clamp(app);
    wave_resize(app);

    window_show(app->window);
    return app;
}

static void i_destroy(App **app)
{
    uint32_t i;
    /* Si queda una exportacion en marcha hay que ESPERARLA. El hilo lee las
       pistas y la propia App, y aqui se destruyen las dos: cerrar la ventana
       mientras se exporta un tramo largo —que es justo la operacion que tarda
       segundos— llegaba a leer memoria ya liberada. `bthread_close` no basta,
       solo suelta el descriptor (osbs/win/bthread.c:44). Lo delato el informe
       de salida del SDK en Linux: "Non-dealloc Threads: 1/0". */
    if ((*app)->job.th != NULL)
    {
        bthread_wait((*app)->job.th);
        bthread_close(&(*app)->job.th);
    }
    /* Despues de esperar al hilo, nunca antes: es el cerrojo con el que ese
       hilo se comunica con la interfaz. */
    if ((*app)->job.mtx != NULL)
        bmutex_close(&(*app)->job.mtx);
    app_play_stop(*app);
    ssb_watch_stop();
    for (i = 0; i < (*app)->ntracks; ++i)
        ssb_track_destroy(&(*app)->tracks[i].track);
    icons_destroy(*app);
    font_destroy(&(*app)->font);
    heap_delete_n(&(*app)->peaks, (*app)->peaks_cap, ssb_peak);
    window_destroy(&(*app)->window);
    heap_delete(app, App);
}

/* Se ejecuta en el hilo de interfaz a la cadencia de osmain_sync. */
/* Carga un guion de comandos: una orden por linea, las vacias y las que
   empiezan por '#' se ignoran. Solo para pruebas, pero pasa por exactamente las
   mismas funciones que los botones. */
static void i_script_load(App *app, const char_t *path)
{
    FILE *f = fopen(path, "rb");
    char line[220];
    if (f == NULL)
    {
        i_say(app, "no encuentro el guion");
        return;
    }
    app->script_n = 0;
    app->script_i = 0;
    app->script_until = 0.0;
    bstd_sprintf(app->script_log, sizeof(app->script_log), "%s.log", path);
    {
        FILE *lg = fopen(app->script_log, "wb");
        if (lg != NULL)
            fclose(lg);
    }
    while (app->script_n < 96 && fgets(line, (int)sizeof(line), f) != NULL)
    {
        char *p = line;
        size_t len;
        /* PowerShell escribe BOM por omision con -Encoding UTF8, y el BOM viaja
           pegado a la primera orden: `add` llegaba como "Ã¯Â»Â¿add" y no
           se reconocia. El sintoma era que el primer comando del guion se
           perdia en silencio, que parece cualquier cosa menos un problema de
           codificacion. */
        if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
            p += 3;
        len = strlen(p);
        while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r'))
            p[--len] = 0;
        if (len == 0 || p[0] == '#')
            continue;
        str_copy_c(app->script[app->script_n], sizeof(app->script[0]), p);
        app->script_n++;
    }
    fclose(f);
}

/* Avanza el guion. `wait <seg>` y `quit` los atiende el propio conductor; todo
   lo demas va a cmd_run, que es el mismo camino que usan los botones. */
static void i_script_step(App *app, const real64_t ctime)
{
    const char_t *line;

    if (app->script_i >= app->script_n)
        return;
    if (app->script_until > 0.0 && ctime < app->script_until)
        return;

    line = app->script[app->script_i];
    app->script_i++;
    app->script_until = 0.0;

    if (str_str(line, "wait ") == line)
    {
        app->script_until = ctime + atof(line + 5);
        cmd_print(app, "> %s", line);
        return;
    }
    if (str_equ_c(line, "quit") == TRUE)
    {
        app_save_settings(app);
        osapp_finish();
        return;
    }
    cmd_run(app, line);
}

static void i_update(App *app, const real64_t prtime, const real64_t ctime)
{
    ssb_time from = 0, to = 0;
    unref(prtime);

    /* Antes que nada, y antes del corte por "no hay pistas": el guion es
       precisamente quien las anade. */
    i_job_collect(app);
    i_script_step(app, ctime);

    /* La reproduccion se cierra sola al llegar al final, para que el boton
       vuelva a decir "Reproducir" sin que nadie tenga que pulsar nada. */
    if (app->play != NULL)
    {
        if (ssb_play_done(app->play) == 1)
            app_play_stop(app);
        else
            view_update(app->view);
    }

    if (app->message[0] != 0)
    {
        if (app->message_at == 0.0)
            app->message_at = ctime;
        else if (ctime - app->message_at > 6.0)
            app->message[0] = 0;
    }

    if (app->ntracks == 0)
        return;

    app_span(app, &from, &to);
    wave_clamp(app);

    /* Estado de los dispositivos, leido de la FOTO del vigilante.
     *
     * Esto lo hacia antes `ssb_output_muted()`, que abre COM y busca el
     * endpoint, aqui mismo, en el hilo de interfaz, una vez por segundo y por
     * pista. Mientras los dispositivos estan quietos no se nota; en cuanto uno
     * desaparece, esas llamadas tardan segundos y el programa se queda
     * congelado. Ahora esto es copiar un par de valores. */
    if (ctime - app->mute_clock >= 0.5)
    {
        uint32_t i;
        uint64_t ver = 0;
        app->mute_clock = ctime;
        for (i = 0; i < app->ntracks; ++i)
        {
            app->tracks[i].sys_muted = ssb_watch_muted(&app->tracks[i].src);
            /* Que la fuente ya no exista es lo que hay que DECIR: sin esto, una
               pista cuyo dispositivo se apago sigue pintando su banda vacia sin
               explicar nada. */
            app->tracks[i].gone = ssb_watch_alive(&app->tracks[i].src) ? FALSE : TRUE;
        }
        /* Y si la lista de fuentes ha cambiado, se rehace el desplegable. Solo
           cuando cambia: reconstruirlo cada medio segundo lo haria imposible de
           desplegar. */
        ssb_watch_sources(NULL, 0, &ver);
        if (ver != app->src_version)
        {
            app->src_version = ver;
            app_reload_sources(app);
        }
    }

    if (ctime - app->label_clock >= 0.25)
    {
        uint64_t disk = 0;
        double ratio = 0.0;
        uint32_t i;
        app->label_clock = ctime;
        for (i = 0; i < app->ntracks; ++i)
        {
            ssb_track_stats st;
            ssb_track_stats_get(app->tracks[i].track, &st);
            disk += st.disk_bytes;
            ratio += st.ratio;
        }
        bstd_sprintf(app->status, sizeof(app->status), T(app, TXT_STATUS),
                     app->ntracks, ssb_time_to_sec(to - from),
                     (double)disk / 1048576.0,
                     app->ntracks ? ratio / (double)app->ntracks : 0.0,
                     app->span_secs, "");
    }

    view_update(app->view);
}

/* La macro del punto de entrada vive en su propia cabecera y se incluye aqui,
   junto al uso: es la convencion del SDK (ver demo/drawbig/drawbig.c:981 y
   test/consumer/main.c:144). `nappgui.h` no la arrastra. */
#include <osapp/osmain.h>

osmain_sync(0.04, i_create, i_destroy, i_update, "", App)
