/* Captura en Linux con PulseAudio.
 *
 * Equivale a win/ssb_capture_win.cpp y cumple el mismo contrato: entrega float
 * intercalado al callback, con el instante del primer frame del paquete y las
 * banderas de silencio y discontinuidad.
 *
 * Las tres clases de fuente se resuelven asi:
 *
 *   salida     -> se graba del monitor del sink (`<sink>.monitor`).
 *   entrada    -> se graba de la source directamente.
 *   aplicacion -> se graba del monitor del sink al que suena esa aplicacion,
 *                 acotado a su sink-input con `pa_stream_set_monitor_stream`.
 *                 Sin null-sink y sin re-rutear la app. Es la decision de
 *                 docs/01, seccion 6.
 *
 * Toda llamada a libpulse va bajo el lock del threaded mainloop. Las consultas
 * (enumerar, mudo) abren y cierran su propia conexion: se llaman una vez por
 * segundo desde el vigilante, y una conexion corta evita tener que mantener
 * estado global vivo entre llamadas.
 */
#include "ssb.h"
#include "ssb_internal.h"

#include <pulse/pulseaudio.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ conexion */

typedef struct
{
    pa_threaded_mainloop *ml;
    pa_context *ctx;
} i_conn;

static void i_ctx_state(pa_context *c, void *ud)
{
    (void)c;
    pa_threaded_mainloop_signal((pa_threaded_mainloop *)ud, 0);
}

static void i_conn_close(i_conn *c)
{
    if (c->ml != NULL)
        pa_threaded_mainloop_stop(c->ml);
    if (c->ctx != NULL)
    {
        pa_context_disconnect(c->ctx);
        pa_context_unref(c->ctx);
        c->ctx = NULL;
    }
    if (c->ml != NULL)
    {
        pa_threaded_mainloop_free(c->ml);
        c->ml = NULL;
    }
}

static ssb_res i_conn_open(i_conn *c)
{
    memset(c, 0, sizeof(*c));

    c->ml = pa_threaded_mainloop_new();
    if (c->ml == NULL)
        return ssb_err_mem;

    c->ctx = pa_context_new(pa_threaded_mainloop_get_api(c->ml), "SystemSoundBuffer");
    if (c->ctx == NULL)
    {
        i_conn_close(c);
        return ssb_err_mem;
    }

    pa_context_set_state_callback(c->ctx, i_ctx_state, c->ml);

    if (pa_threaded_mainloop_start(c->ml) < 0)
    {
        i_conn_close(c);
        return ssb_err_platform;
    }

    pa_threaded_mainloop_lock(c->ml);
    if (pa_context_connect(c->ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0)
    {
        pa_threaded_mainloop_unlock(c->ml);
        i_conn_close(c);
        return ssb_err_platform;
    }

    for (;;)
    {
        pa_context_state_t st = pa_context_get_state(c->ctx);
        if (st == PA_CONTEXT_READY)
            break;
        if (st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED)
        {
            pa_threaded_mainloop_unlock(c->ml);
            i_conn_close(c);
            return ssb_err_platform;
        }
        pa_threaded_mainloop_wait(c->ml);
    }
    pa_threaded_mainloop_unlock(c->ml);
    return ssb_ok;
}

/* Espera a que termine una operacion. Hay que llamarla con el lock cogido. */
static void i_wait(i_conn *c, pa_operation *op)
{
    if (op == NULL)
        return;
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(c->ml);
    pa_operation_unref(op);
}

/* --------------------------------------------------------------- enumeracion */

typedef struct
{
    i_conn *conn;
    ssb_source *out;
    uint32_t cap;
    uint32_t n; /* cuantas hay en total, aunque no quepan */
    char def_sink[SSB_NAME_MAX];
    char def_source[SSB_NAME_MAX];
    /* Para resolver "esta app suena por este sink" hace falta el nombre legible
       del sink, y los sink-inputs solo traen su indice. */
    uint32_t sink_index[64];
    char sink_desc[64][SSB_NAME_MAX];
    uint32_t nsinks;
} i_collect;

static void i_add(i_collect *co, const ssb_source *s)
{
    if (co->n < co->cap && co->out != NULL)
        co->out[co->n] = *s;
    co->n++;
}

static void i_cb_server(pa_context *c, const pa_server_info *i, void *ud)
{
    i_collect *co = (i_collect *)ud;
    (void)c;
    if (i != NULL)
    {
        if (i->default_sink_name != NULL)
            snprintf(co->def_sink, SSB_NAME_MAX, "%s", i->default_sink_name);
        if (i->default_source_name != NULL)
            snprintf(co->def_source, SSB_NAME_MAX, "%s", i->default_source_name);
    }
    pa_threaded_mainloop_signal(co->conn->ml, 0);
}

static void i_cb_sink(pa_context *c, const pa_sink_info *i, int eol, void *ud)
{
    i_collect *co = (i_collect *)ud;
    ssb_source s;
    (void)c;

    if (eol != 0)
    {
        pa_threaded_mainloop_signal(co->conn->ml, 0);
        return;
    }
    if (i == NULL)
        return;

    memset(&s, 0, sizeof(s));
    s.kind = ssb_src_output_device;
    snprintf(s.id, SSB_NAME_MAX, "%s", i->name);
    snprintf(s.name, SSB_NAME_MAX, "%s", (i->description != NULL) ? i->description : i->name);
    s.active = 1;
    i_add(co, &s);

    if (co->nsinks < 64)
    {
        co->sink_index[co->nsinks] = i->index;
        snprintf(co->sink_desc[co->nsinks], SSB_NAME_MAX, "%s",
                 (i->description != NULL) ? i->description : i->name);
        co->nsinks++;
    }
}

static void i_cb_source(pa_context *c, const pa_source_info *i, int eol, void *ud)
{
    i_collect *co = (i_collect *)ud;
    ssb_source s;
    (void)c;

    if (eol != 0)
    {
        pa_threaded_mainloop_signal(co->conn->ml, 0);
        return;
    }
    if (i == NULL)
        return;

    /* Los monitores no son entradas: son la salida de un sink vista del reves,
       y esa ya sale en la lista como dispositivo de salida. Si se listaran
       tambien aqui, cada altavoz apareceria dos veces. */
    if (i->monitor_of_sink != PA_INVALID_INDEX)
        return;

    memset(&s, 0, sizeof(s));
    s.kind = ssb_src_input_device;
    snprintf(s.id, SSB_NAME_MAX, "%s", i->name);
    snprintf(s.name, SSB_NAME_MAX, "%s", (i->description != NULL) ? i->description : i->name);
    s.active = 1;
    i_add(co, &s);
}

static void i_cb_sink_input(pa_context *c, const pa_sink_input_info *i, int eol, void *ud)
{
    i_collect *co = (i_collect *)ud;
    ssb_source s;
    const char *pid;
    const char *app;
    uint32_t k;
    (void)c;

    if (eol != 0)
    {
        pa_threaded_mainloop_signal(co->conn->ml, 0);
        return;
    }
    if (i == NULL)
        return;

    memset(&s, 0, sizeof(s));
    s.kind = ssb_src_process;

    pid = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_PROCESS_ID);
    s.pid = (pid != NULL) ? (uint32_t)strtoul(pid, NULL, 10) : 0;

    app = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_PROCESS_BINARY);
    if (app == NULL)
        app = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME);
    if (app == NULL)
        app = (i->name != NULL) ? i->name : "?";
    snprintf(s.name, SSB_NAME_MAX, "%s", app);

    /* El id lleva el indice del sink-input: es lo que hace falta para acotar el
       monitor a esta aplicacion, y un pid no basta porque una app puede tener
       varios flujos abiertos. */
    snprintf(s.id, SSB_NAME_MAX, "%u", (unsigned)i->index);

    for (k = 0; k < co->nsinks; ++k)
    {
        if (co->sink_index[k] == i->sink)
        {
            snprintf(s.endpoint, SSB_NAME_MAX, "%s", co->sink_desc[k]);
            break;
        }
    }

    /* `corked` es "pausado". Un flujo abierto pero pausado no esta sonando. */
    s.active = (i->corked != 0) ? 0 : 1;
    i_add(co, &s);
}

uint32_t ssb_enumerate(ssb_source *out, uint32_t cap)
{
    i_conn conn;
    i_collect co;
    uint32_t total;

    if (i_conn_open(&conn) != ssb_ok)
        return 0;

    memset(&co, 0, sizeof(co));
    co.conn = &conn;
    co.out = out;
    co.cap = cap;

    pa_threaded_mainloop_lock(conn.ml);
    i_wait(&conn, pa_context_get_server_info(conn.ctx, i_cb_server, &co));
    i_wait(&conn, pa_context_get_sink_info_list(conn.ctx, i_cb_sink, &co));
    i_wait(&conn, pa_context_get_source_info_list(conn.ctx, i_cb_source, &co));
    i_wait(&conn, pa_context_get_sink_input_info_list(conn.ctx, i_cb_sink_input, &co));
    pa_threaded_mainloop_unlock(conn.ml);

    total = co.n;
    i_conn_close(&conn);
    return total;
}

/* ------------------------------------------------------------------ el mudo */

typedef struct
{
    i_conn *conn;
    int muted;
    int found;
    /* Solo para resolver el nombre del sink por omision. */
    char def_sink[SSB_NAME_MAX];
} i_mute;

static void i_cb_mute_server(pa_context *c, const pa_server_info *i, void *ud)
{
    i_mute *m = (i_mute *)ud;
    (void)c;
    if (i != NULL && i->default_sink_name != NULL)
        snprintf(m->def_sink, SSB_NAME_MAX, "%s", i->default_sink_name);
    pa_threaded_mainloop_signal(m->conn->ml, 0);
}

static void i_cb_mute_sink(pa_context *c, const pa_sink_info *i, int eol, void *ud)
{
    i_mute *m = (i_mute *)ud;
    (void)c;

    if (eol != 0)
    {
        pa_threaded_mainloop_signal(m->conn->ml, 0);
        return;
    }
    if (i == NULL)
        return;

    m->found = 1;
    m->muted = (i->mute != 0 || pa_cvolume_max(&i->volume) == 0) ? 1 : 0;
}

int ssb_output_muted(const ssb_source *src)
{
    i_conn conn;
    i_mute m;
    int res;

    if (src == NULL || src->kind != ssb_src_output_device)
        return 0;

    if (i_conn_open(&conn) != ssb_ok)
        return 0;

    memset(&m, 0, sizeof(m));
    m.conn = &conn;

    pa_threaded_mainloop_lock(conn.ml);
    if (src->id[0] == 0)
    {
        i_wait(&conn, pa_context_get_server_info(conn.ctx, i_cb_mute_server, &m));
        if (m.def_sink[0] != 0)
            i_wait(&conn, pa_context_get_sink_info_by_name(conn.ctx, m.def_sink, i_cb_mute_sink, &m));
    }
    else
    {
        i_wait(&conn, pa_context_get_sink_info_by_name(conn.ctx, src->id, i_cb_mute_sink, &m));
    }
    pa_threaded_mainloop_unlock(conn.ml);

    res = (m.found != 0) ? m.muted : 0;
    i_conn_close(&conn);
    return res;
}

/* ------------------------------------------------------------ parseo de spec */

/* Un nombre de dispositivo servible.
 *
 * PulseAudio devuelve el nombre real del dispositivo por omision. PipeWire
 * puede devolver el marcador `@DEFAULT_SINK@` sin resolver —pasa cuando no hay
 * politica de sesion que haya elegido uno, por ejemplo con un sink virtual
 * recien creado— y encima NO lo resuelve si se le pregunta por el:
 *
 *     $ pactl get-default-sink
 *     @DEFAULT_SINK@
 *     $ pactl get-sink-volume @DEFAULT_SINK@
 *     Failed to get sink information: No such entity
 *
 * Asi que cualquier nombre que empiece por '@' es un marcador, no un
 * dispositivo, y hay que caer al primero que haya en vez de rendirse. */
static int i_name_usable(const char *n)
{
    return (n != NULL && n[0] != 0 && n[0] != '@') ? 1 : 0;
}

/* Nombre legible del dispositivo por omision. Es lo unico que la parte portable
   no puede saber, porque hay que preguntarselo al servidor. */
static void i_default_name(ssb_src_kind kind, char *out, uint32_t size)
{
    i_conn conn;
    i_collect co;
    static ssb_source list[SSB_WATCH_MAX];
    uint32_t i;

    out[0] = 0;
    if (kind == ssb_src_process)
        return;
    if (i_conn_open(&conn) != ssb_ok)
        return;

    memset(&co, 0, sizeof(co));
    co.conn = &conn;
    co.out = list;
    co.cap = SSB_WATCH_MAX;

    pa_threaded_mainloop_lock(conn.ml);
    i_wait(&conn, pa_context_get_server_info(conn.ctx, i_cb_server, &co));
    i_wait(&conn, pa_context_get_sink_info_list(conn.ctx, i_cb_sink, &co));
    i_wait(&conn, pa_context_get_source_info_list(conn.ctx, i_cb_source, &co));
    pa_threaded_mainloop_unlock(conn.ml);

    for (i = 0; i < co.n && i < SSB_WATCH_MAX; ++i)
    {
        const char *want = (kind == ssb_src_output_device) ? co.def_sink : co.def_source;
        if (list[i].kind == kind && i_name_usable(want) && strcmp(list[i].id, want) == 0)
        {
            snprintf(out, size, "%s", list[i].name);
            break;
        }
    }
    /* Sin dispositivo por omision servible, el primero de esa clase. Es lo que
       de verdad se va a abrir (ver el respaldo de ssb_capture_open), asi que el
       nombre que se ensena tiene que ser ese y no "por omision". */
    if (out[0] == 0)
    {
        for (i = 0; i < co.n && i < SSB_WATCH_MAX; ++i)
        {
            if (list[i].kind == kind)
            {
                snprintf(out, size, "%s", list[i].name);
                break;
            }
        }
    }
    i_conn_close(&conn);
}

ssb_res ssb_source_parse(const char *spec, ssb_source *out)
{
    return _ssb_source_select(spec, i_default_name, out);
}

/* -------------------------------------------------------------------- captura */

struct ssb_capture_t
{
    i_conn conn;
    pa_stream *st;
    ssb_audio_fn fn;
    void *ctx;
    uint32_t channels;
    uint32_t rate;
    /* Un hueco lo avisa `pa_stream_peek` devolviendo datos nulos. La bandera se
       arrastra hasta el siguiente paquete con audio, que es donde el motor la
       espera. */
    int pending_disc;
    /* float32 nativo es lo que se pide al servidor, asi que el paquete se pasa
       tal cual y no hace falta convertir. Este hueco solo se usa si el servidor
       entrega el paquete partido. */
    float *buf;
    size_t buf_cap;
    /* Donde acabo el paquete anterior, si el flujo viene siendo continuo.
       Sirve para no sellar tarde un paquete corto: ver i_stream_read. */
    ssb_time t_next;
};

/* Cuanto se acepta corregir hacia atras el sello de un paquete. Por encima de
   esto se cree al reloj: es un hueco de verdad y la linea de tiempo del motor
   tiene que verlo. El liston del motor son 8 ms sostenidos tres paquetes
   (ssb_track.c:26), y las desviaciones medidas aqui llegan a 20 ms, asi que el
   margen tiene que cubrirlas sin tragarse un corte real. */
#define SSB_PA_SLACK_MS 40

/* Resuelve, para una fuente, de que source hay que grabar y —si es una
   aplicacion— a que sink-input hay que acotar el monitor. */
typedef struct
{
    i_conn *conn;
    char source[SSB_NAME_MAX * 2];
    uint32_t monitor_of; /* PA_INVALID_INDEX si no hay que acotar */
    uint32_t want_sink;  /* indice del sink que se busca */
    uint32_t want_input; /* indice del sink-input que se busca */
    uint32_t rate;
    uint8_t channels;
    int found;
} i_resolve;

static void i_cb_res_server(pa_context *c, const pa_server_info *i, void *ud)
{
    i_resolve *r = (i_resolve *)ud;
    (void)c;
    if (i != NULL)
    {
        if (r->source[0] == 0 && i_name_usable(i->default_sink_name))
            snprintf(r->source, sizeof(r->source), "%s.monitor", i->default_sink_name);
    }
    pa_threaded_mainloop_signal(r->conn->ml, 0);
}

static void i_cb_res_defsource(pa_context *c, const pa_server_info *i, void *ud)
{
    i_resolve *r = (i_resolve *)ud;
    (void)c;
    if (i != NULL && i_name_usable(i->default_source_name))
        snprintf(r->source, sizeof(r->source), "%s", i->default_source_name);
    pa_threaded_mainloop_signal(r->conn->ml, 0);
}

/* Respaldo cuando no hay dispositivo por omision servible: el primero que haya. */
static void i_cb_res_firstsink(pa_context *c, const pa_sink_info *i, int eol, void *ud)
{
    i_resolve *r = (i_resolve *)ud;
    (void)c;
    if (eol != 0)
    {
        pa_threaded_mainloop_signal(r->conn->ml, 0);
        return;
    }
    if (i == NULL || r->source[0] != 0 || !i_name_usable(i->name))
        return;
    snprintf(r->source, sizeof(r->source), "%s.monitor", i->name);
}

static void i_cb_res_firstsource(pa_context *c, const pa_source_info *i, int eol, void *ud)
{
    i_resolve *r = (i_resolve *)ud;
    (void)c;
    if (eol != 0)
    {
        pa_threaded_mainloop_signal(r->conn->ml, 0);
        return;
    }
    if (i == NULL || r->source[0] != 0 || !i_name_usable(i->name))
        return;
    /* Un monitor no es una entrada: es la salida de un sink vista del reves. */
    if (i->monitor_of_sink != PA_INVALID_INDEX)
        return;
    snprintf(r->source, sizeof(r->source), "%s", i->name);
}

static void i_cb_res_sinkinput(pa_context *c, const pa_sink_input_info *i, int eol, void *ud)
{
    i_resolve *r = (i_resolve *)ud;
    (void)c;
    if (eol != 0)
    {
        pa_threaded_mainloop_signal(r->conn->ml, 0);
        return;
    }
    if (i == NULL || r->found != 0)
        return;
    if (i->index != r->want_input)
        return;
    r->want_sink = i->sink;
    r->found = 1;
}

static void i_cb_res_sinkname(pa_context *c, const pa_sink_info *i, int eol, void *ud)
{
    i_resolve *r = (i_resolve *)ud;
    (void)c;
    if (eol != 0)
    {
        pa_threaded_mainloop_signal(r->conn->ml, 0);
        return;
    }
    if (i == NULL || i->index != r->want_sink)
        return;
    snprintf(r->source, sizeof(r->source), "%s.monitor", i->name);
}

static void i_cb_res_spec(pa_context *c, const pa_source_info *i, int eol, void *ud)
{
    i_resolve *r = (i_resolve *)ud;
    (void)c;
    if (eol != 0)
    {
        pa_threaded_mainloop_signal(r->conn->ml, 0);
        return;
    }
    if (i == NULL)
        return;
    r->rate = i->sample_spec.rate;
    r->channels = i->sample_spec.channels;
}

static void i_stream_success(pa_stream *s, int success, void *ud)
{
    (void)s;
    (void)success;
    pa_threaded_mainloop_signal((pa_threaded_mainloop *)ud, 0);
}

static void i_stream_state(pa_stream *s, void *ud)
{
    (void)s;
    pa_threaded_mainloop_signal((pa_threaded_mainloop *)ud, 0);
}

static void i_stream_read(pa_stream *s, size_t nbytes, void *ud)
{
    ssb_capture *c = (ssb_capture *)ud;
    (void)nbytes;

    for (;;)
    {
        const void *data = NULL;
        size_t bytes = 0;

        if (pa_stream_peek(s, &data, &bytes) < 0)
            return;
        if (bytes == 0)
            return;

        if (data == NULL)
        {
            /* Hueco. El servidor dice cuantos bytes faltan pero no los tiene:
               no hay nada que entregar, solo que avisar. Y se rompe la cadena:
               lo que venga detras ya no es contiguo con lo de antes, asi que
               vuelve a sellarse contra el reloj. */
            c->pending_disc = 1;
            c->t_next = 0;
            pa_stream_drop(s);
            continue;
        }

        {
            uint32_t frames = (uint32_t)(bytes / (sizeof(float) * c->channels));
            if (frames > 0)
            {
                /* El instante del PRIMER frame del paquete: ahora menos lo que
                   dura el propio paquete. El callback salta en cuanto hay datos,
                   asi que lo que queda fuera de esa cuenta es un desfase pequeno
                   y CONSTANTE, que no estira ni encoge el tramo.
                
                   No se usa `pa_stream_get_latency`. Se probaron las tres
                   variantes sobre 24 s de audio:
                     sin informacion de tiempos ->  44176 Hz de 44100 (+0.17 %)
                     con foto de tiempos vieja  ->  22075 Hz          (-49.9 %)
                     con tiempos interpolados   ->  42771 Hz          (-3.0 %)
                   La latencia que reporta el servidor en el primer paquete de un
                   monitor no se corresponde con la edad real de esos datos.

                   Con esta cuenta queda un desfase residual de unos 35 ms: lo
                   que el fragmento espera entre capturarse y llegar al callback.
                   Es constante, asi que NO afecta a la alineacion entre pistas,
                   que es para lo que sirve la linea de tiempo aqui; solo corre
                   35 ms el origen absoluto. WASAPI da un sello por paquete y no
                   necesita esto; PulseAudio no ofrece equivalente fiable. */
                /* `ahora - duracion` solo vale si el paquete trae datos recien
                   producidos. PulseAudio entrega a menudo un trozo largo y un
                   resto corto, y el corto NO es fresco: es la cola de lo que ya
                   habia, que ha esperado. Al restarle solo SU duracion se sella
                   hasta 20 ms tarde, y la marca llega a ir hacia atras respecto
                   del paquete anterior. Medido con `ssb drift` sobre 12 s: 46
                   saltos de mas de 5 ms, todos coincidiendo con un paquete
                   corto. Eso es lo que re-anclaba la linea de tiempo del motor
                   y contaba un hueco que no habia ocurrido.

                   Un flujo continuo avanza exactamente `frames`, asi que si el
                   sello cae DESPUES de donde acabo el anterior y la diferencia
                   es pequena, manda la continuidad. Solo se corrige en ese
                   sentido: si cae antes, es que la fuente va rapida —deriva, no
                   hueco— y ahi manda el reloj, que es lo que impide que esto se
                   convierta en una cuenta de frames a secas y se separe sola
                   (docs/03). */
                ssb_time dur = (ssb_time)frames * SSB_TICKS_PER_SEC / (ssb_time)c->rate;
                ssb_time now = ssb_now();
                ssb_time t = (now > dur) ? (now - dur) : 0;
                ssb_time slack = (ssb_time)SSB_PA_SLACK_MS * SSB_TICKS_PER_SEC / 1000;

                if (c->t_next != 0 && t > c->t_next && (t - c->t_next) < slack)
                    t = c->t_next;
                c->t_next = t + dur;
                c->fn(c->ctx, (const float *)data, frames, t, 0, c->pending_disc);
                c->pending_disc = 0;
            }
        }
        pa_stream_drop(s);
    }
}

ssb_res ssb_capture_open(const ssb_source *src, ssb_audio_fn fn, void *ctx,
                         uint32_t *channels, uint32_t *rate, ssb_capture **out)
{
    ssb_capture *c;
    i_resolve r;
    pa_sample_spec spec;
    pa_buffer_attr attr;
    ssb_res res;

    if (src == NULL || fn == NULL || out == NULL || channels == NULL || rate == NULL)
        return ssb_err_arg;

    c = (ssb_capture *)calloc(1, sizeof(ssb_capture));
    if (c == NULL)
        return ssb_err_mem;
    c->fn = fn;
    c->ctx = ctx;

    res = i_conn_open(&c->conn);
    if (res != ssb_ok)
    {
        free(c);
        return res;
    }

    memset(&r, 0, sizeof(r));
    r.conn = &c->conn;
    r.monitor_of = PA_INVALID_INDEX;
    r.want_sink = PA_INVALID_INDEX;
    r.want_input = PA_INVALID_INDEX;

    pa_threaded_mainloop_lock(c->conn.ml);

    if (src->kind == ssb_src_process)
    {
        /* El id de una fuente de aplicacion lleva el indice del sink-input. */
        r.want_input = (src->id[0] != 0) ? (uint32_t)strtoul(src->id, NULL, 10) : PA_INVALID_INDEX;
        if (r.want_input == PA_INVALID_INDEX)
        {
            pa_threaded_mainloop_unlock(c->conn.ml);
            ssb_capture_close(&c);
            return ssb_err_notfound;
        }
        i_wait(&c->conn, pa_context_get_sink_input_info_list(c->conn.ctx, i_cb_res_sinkinput, &r));
        if (r.found == 0)
        {
            pa_threaded_mainloop_unlock(c->conn.ml);
            ssb_capture_close(&c);
            return ssb_err_notfound;
        }
        i_wait(&c->conn, pa_context_get_sink_info_list(c->conn.ctx, i_cb_res_sinkname, &r));
        r.monitor_of = r.want_input;
    }
    else if (src->kind == ssb_src_output_device)
    {
        if (src->id[0] != 0)
        {
            snprintf(r.source, sizeof(r.source), "%s.monitor", src->id);
        }
        else
        {
            i_wait(&c->conn, pa_context_get_server_info(c->conn.ctx, i_cb_res_server, &r));
            if (r.source[0] == 0)
                i_wait(&c->conn, pa_context_get_sink_info_list(c->conn.ctx, i_cb_res_firstsink, &r));
        }
    }
    else
    {
        if (src->id[0] != 0)
        {
            snprintf(r.source, sizeof(r.source), "%s", src->id);
        }
        else
        {
            i_wait(&c->conn, pa_context_get_server_info(c->conn.ctx, i_cb_res_defsource, &r));
            if (r.source[0] == 0)
                i_wait(&c->conn, pa_context_get_source_info_list(c->conn.ctx, i_cb_res_firstsource, &r));
        }
    }

    if (r.source[0] == 0)
    {
        pa_threaded_mainloop_unlock(c->conn.ml);
        ssb_capture_close(&c);
        return ssb_err_notfound;
    }

    /* El formato nativo de la source, para no meter un remuestreo que nadie ha
       pedido. Los canales se recortan a lo que sabe guardar el motor. */
    i_wait(&c->conn, pa_context_get_source_info_by_name(c->conn.ctx, r.source, i_cb_res_spec, &r));
    if (r.rate == 0 || r.channels == 0)
    {
        pa_threaded_mainloop_unlock(c->conn.ml);
        ssb_capture_close(&c);
        return ssb_err_notfound;
    }

    spec.format = PA_SAMPLE_FLOAT32NE;
    spec.rate = r.rate;
    spec.channels = (r.channels > SSB_MAX_CHANNELS) ? (uint8_t)SSB_MAX_CHANNELS : r.channels;

    c->channels = spec.channels;
    c->rate = spec.rate;

    c->st = pa_stream_new(c->conn.ctx, "capture", &spec, NULL);
    if (c->st == NULL)
    {
        pa_threaded_mainloop_unlock(c->conn.ml);
        ssb_capture_close(&c);
        return ssb_err_platform;
    }

    pa_stream_set_state_callback(c->st, i_stream_state, c->conn.ml);
    pa_stream_set_read_callback(c->st, i_stream_read, c);

    /* Acotar el monitor a un sink-input. Tiene que ir ANTES de conectar. */
    if (r.monitor_of != PA_INVALID_INDEX)
    {
        if (pa_stream_set_monitor_stream(c->st, r.monitor_of) < 0)
        {
            pa_threaded_mainloop_unlock(c->conn.ml);
            ssb_capture_close(&c);
            return ssb_err_platform;
        }
    }

    /* Bloques cortos: el motor coloca por tiempo y prefiere paquetes pequenos y
       frecuentes a uno grande cada mucho. -1 en el resto deja decidir al
       servidor, que es lo recomendado con ADJUST_LATENCY. */
    memset(&attr, 0xff, sizeof(attr));
    attr.fragsize = (uint32_t)(pa_usec_to_bytes(20000, &spec));

    /* AUTO_TIMING_UPDATE + INTERPOLATE_TIMING no son opcionales aqui: sin ellas
       `pa_stream_get_latency` responde a partir de la ultima foto de tiempos que
       tenga, y esa foto no se refresca sola. La latencia crecia entonces un
       segundo por segundo y el tramo salia del doble de largo. Medido: 24 s de
       audio daban una ventana de 47.98 s. */
    if (pa_stream_connect_record(c->st, r.source, &attr,
                                 (pa_stream_flags_t)(PA_STREAM_ADJUST_LATENCY |
                                                     PA_STREAM_AUTO_TIMING_UPDATE |
                                                     PA_STREAM_INTERPOLATE_TIMING)) < 0)
    {
        pa_threaded_mainloop_unlock(c->conn.ml);
        ssb_capture_close(&c);
        return ssb_err_platform;
    }

    for (;;)
    {
        pa_stream_state_t st = pa_stream_get_state(c->st);
        if (st == PA_STREAM_READY)
            break;
        if (st == PA_STREAM_FAILED || st == PA_STREAM_TERMINATED)
        {
            pa_threaded_mainloop_unlock(c->conn.ml);
            ssb_capture_close(&c);
            return ssb_err_platform;
        }
        pa_threaded_mainloop_wait(c->conn.ml);
    }

    /* La latencia no vale hasta que llega la primera informacion de tiempo del
       servidor. Sin esperarla, el primer paquete se sella con la hora de ahora
       en vez de con la de cuando se capturo, y el tramo entero queda ~40 ms
       corto por delante. Medido: 24.06 s de audio daban 44176 Hz efectivos
       sobre 44100 nominales, y el error absoluto era el mismo (41 ms) grabando
       3 s que grabando 24. */
    i_wait(&c->conn, pa_stream_update_timing_info(c->st, i_stream_success, c->conn.ml));

    pa_threaded_mainloop_unlock(c->conn.ml);

    *channels = c->channels;
    *rate = c->rate;
    *out = c;
    return ssb_ok;
}

void ssb_capture_close(ssb_capture **cp)
{
    ssb_capture *c;

    if (cp == NULL || *cp == NULL)
        return;
    c = *cp;

    if (c->st != NULL)
    {
        pa_threaded_mainloop_lock(c->conn.ml);
        pa_stream_set_read_callback(c->st, NULL, NULL);
        pa_stream_set_state_callback(c->st, NULL, NULL);
        pa_stream_disconnect(c->st);
        pa_stream_unref(c->st);
        c->st = NULL;
        pa_threaded_mainloop_unlock(c->conn.ml);
    }

    i_conn_close(&c->conn);
    free(c->buf);
    free(c);
    *cp = NULL;
}
