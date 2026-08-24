/* Vigilante de dispositivos: quien pregunta al sistema, y desde donde.
 *
 * Antes la interfaz llamaba a `ssb_output_muted()` una vez por segundo y por
 * pista, DESDE EL HILO DE INTERFAZ. Cada llamada abre un enumerador COM, busca
 * el endpoint y activa su control de volumen. Con los dispositivos quietos no se
 * nota; en cuanto uno desaparece —unos auriculares Bluetooth que se apagan— esas
 * llamadas pueden tardar segundos, y el programa se queda congelado con la
 * ventana pintada. Le paso al usuario y tuvo que reiniciarlo.
 *
 * Aqui el trabajo caro se hace en un hilo propio y se publica una FOTO. La
 * interfaz solo copia esa foto, que es memoria y un mutex: no puede bloquearse
 * por mucho que el sistema de audio se atasque.
 *
 * De paso resuelve lo otro: como la foto se rehace sola, las entradas y salidas
 * que aparecen o desaparecen se reflejan sin que nadie pulse nada. `version`
 * cambia cuando la lista cambia, para que la interfaz sepa cuando reconstruir
 * su desplegable en vez de hacerlo en cada fotograma.
 */
#include "ssb.h"
#include "ssb_internal.h"

#include <stdlib.h>
#include <string.h>

#define WATCH_PERIOD_MS 1000

typedef struct
{
    ssb_mutex *mtx;
    ssb_thread *th;
    volatile int stop;
    int running;

    ssb_source list[SSB_WATCH_MAX];
    uint8_t muted[SSB_WATCH_MAX];
    uint32_t count;
    uint64_t version;
} i_watch;

static i_watch i_W;

/* Dos fotos son "la misma lista" si traen las mismas fuentes en el mismo orden.
   Se compara por tipo, id y pid: el nombre puede cambiar de mayusculas sin que
   haya pasado nada, y el estado activo cambia cada dos por tres. */
static int i_same_list(const ssb_source *a, uint32_t na, const ssb_source *b, uint32_t nb)
{
    uint32_t i;
    if (na != nb)
        return 0;
    for (i = 0; i < na; ++i)
    {
        if (a[i].kind != b[i].kind || a[i].pid != b[i].pid)
            return 0;
        if (strcmp(a[i].id, b[i].id) != 0)
            return 0;
    }
    return 1;
}

static void i_watch_thread(void *ctx)
{
    ssb_source snap[SSB_WATCH_MAX];
    uint8_t mute[SSB_WATCH_MAX];
    (void)ctx;

    while (i_W.stop == 0)
    {
        uint32_t n = ssb_enumerate(snap, SSB_WATCH_MAX);
        uint32_t i;
        int changed;

        if (n > SSB_WATCH_MAX)
            n = SSB_WATCH_MAX;
        for (i = 0; i < n; ++i)
            mute[i] = (uint8_t)(ssb_output_muted(&snap[i]) ? 1 : 0);

        ssb_mutex_lock(i_W.mtx);
        changed = i_same_list(i_W.list, i_W.count, snap, n) ? 0 : 1;
        memcpy(i_W.list, snap, (size_t)n * sizeof(ssb_source));
        memcpy(i_W.muted, mute, (size_t)n);
        i_W.count = n;
        /* La version solo sube cuando cambia la LISTA. Si subiera tambien al
           cambiar el estado de mudo, la interfaz reconstruiria el desplegable
           cada segundo y no se podria ni desplegarlo. */
        if (changed)
            i_W.version++;
        ssb_mutex_unlock(i_W.mtx);

        /* Se despierta a menudo para poder salir rapido, no para mirar mas. */
        for (i = 0; i < WATCH_PERIOD_MS / 50 && i_W.stop == 0; ++i)
            ssb_sleep_ms(50);
    }
}

ssb_res ssb_watch_start(void)
{
    if (i_W.running != 0)
        return ssb_ok;
    memset(&i_W, 0, sizeof(i_W));
    i_W.mtx = ssb_mutex_create();
    if (i_W.mtx == NULL)
        return ssb_err_mem;
    i_W.th = ssb_thread_start(i_watch_thread, NULL);
    if (i_W.th == NULL)
    {
        ssb_mutex_destroy(&i_W.mtx);
        return ssb_err_platform;
    }
    i_W.running = 1;
    return ssb_ok;
}

void ssb_watch_stop(void)
{
    if (i_W.running == 0)
        return;
    i_W.stop = 1;
    ssb_thread_join(&i_W.th);
    ssb_mutex_destroy(&i_W.mtx);
    i_W.running = 0;
}

uint32_t ssb_watch_sources(ssb_source *out, uint32_t cap, uint64_t *version)
{
    uint32_t n = 0;
    if (i_W.running == 0)
        return 0;
    ssb_mutex_lock(i_W.mtx);
    n = (i_W.count < cap) ? i_W.count : cap;
    if (out != NULL && n > 0)
        memcpy(out, i_W.list, (size_t)n * sizeof(ssb_source));
    if (version != NULL)
        *version = i_W.version;
    ssb_mutex_unlock(i_W.mtx);
    return n;
}

int ssb_watch_muted(const ssb_source *src)
{
    uint32_t i;
    int res = 0;
    if (src == NULL || i_W.running == 0)
        return 0;
    if (src->kind != ssb_src_output_device)
        return 0;
    ssb_mutex_lock(i_W.mtx);
    for (i = 0; i < i_W.count; ++i)
    {
        if (i_W.list[i].kind == src->kind && strcmp(i_W.list[i].id, src->id) == 0)
        {
            res = (i_W.muted[i] != 0) ? 1 : 0;
            break;
        }
    }
    ssb_mutex_unlock(i_W.mtx);
    return res;
}

int ssb_watch_alive(const ssb_source *src)
{
    uint32_t i;
    int res = 0;
    if (src == NULL || i_W.running == 0)
        return 1; /* sin vigilante no se puede afirmar que falte: se da por viva */
    /* El dispositivo POR OMISION se guarda con el id vacio: no hay nada que
       comparar contra la lista, y siempre hay un predeterminado. Decir que ha
       desaparecido seria mentir en el caso mas comun de todos. */
    if (src->kind != ssb_src_process && src->id[0] == 0)
        return 1;
    ssb_mutex_lock(i_W.mtx);
    /* Foto aun vacia = todavia no se sabe, NO "ha desaparecido". El vigilante
       tarda su primer ciclo en publicar, y durante ese rato toda pista se
       marcaba como perdida nada mas crearse. */
    if (i_W.count == 0)
    {
        ssb_mutex_unlock(i_W.mtx);
        return 1;
    }
    for (i = 0; i < i_W.count; ++i)
    {
        if (i_W.list[i].kind != src->kind)
            continue;
        if (src->kind == ssb_src_process)
        {
            if (i_W.list[i].pid == src->pid)
            {
                res = 1;
                break;
            }
        }
        else if (strcmp(i_W.list[i].id, src->id) == 0)
        {
            res = 1;
            break;
        }
    }
    ssb_mutex_unlock(i_W.mtx);
    return res;
}
