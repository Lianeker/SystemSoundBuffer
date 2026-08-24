/* Utiles portables: reloj, hilos, mutex y directorios. */
#include "ssb.h"
#include "ssb_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#else
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#endif

const char *ssb_res_str(ssb_res r)
{
    switch (r)
    {
    case ssb_ok:
        return "ok";
    case ssb_err_arg:
        return "argumento invalido";
    case ssb_err_io:
        return "error de entrada/salida";
    case ssb_err_mem:
        return "sin memoria";
    case ssb_err_platform:
        return "error del sistema";
    case ssb_err_notfound:
        return "no encontrado";
    case ssb_err_empty:
        return "vacio";
    case ssb_err_format:
        return "formatos que no se pueden juntar";
    default:
        return "?";
    }
}

double ssb_time_to_sec(ssb_time t)
{
    return (double)t / (double)SSB_TICKS_PER_SEC;
}

/* --------------------------------------------------------------------- reloj */

ssb_time ssb_now(void)
{
#if defined(_WIN32)
    /* Mismo reloj que reporta WASAPI en qpcPosition: QPC en unidades de 100 ns. */
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (ssb_time)((double)c.QuadPart * (double)SSB_TICKS_PER_SEC / (double)f.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ssb_time)ts.tv_sec * SSB_TICKS_PER_SEC + (ssb_time)(ts.tv_nsec / 100);
#endif
}

/* Ancla entre la linea de tiempo (QPC) y la hora de pared. Se fija una vez, en
   la primera consulta, y a partir de ahi todo se deriva por diferencia: asi un
   instante del buffer se traduce a una hora concreta sin depender de que el
   reloj del sistema no se haya movido. */
static int i_epoch_set = 0;
static ssb_time i_epoch_qpc = 0;
static int64_t i_epoch_unix = 0; /* segundos desde 1970 */

static void i_epoch_init(void)
{
    if (i_epoch_set)
        return;
    i_epoch_qpc = ssb_now();
    i_epoch_unix = (int64_t)time(NULL);
    i_epoch_set = 1;
}

void ssb_wall_clock(ssb_time t, int *year, int *month, int *day,
                    int *hour, int *minute, int *second)
{
    int64_t secs;
    time_t tt;
    struct tm lt;

    i_epoch_init();
    if (t >= i_epoch_qpc)
        secs = i_epoch_unix + (int64_t)((t - i_epoch_qpc) / SSB_TICKS_PER_SEC);
    else
        secs = i_epoch_unix - (int64_t)((i_epoch_qpc - t) / SSB_TICKS_PER_SEC);
    tt = (time_t)secs;
#if defined(_WIN32)
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    if (year != NULL)
        *year = lt.tm_year + 1900;
    if (month != NULL)
        *month = lt.tm_mon + 1;
    if (day != NULL)
        *day = lt.tm_mday;
    if (hour != NULL)
        *hour = lt.tm_hour;
    if (minute != NULL)
        *minute = lt.tm_min;
    if (second != NULL)
        *second = lt.tm_sec;
}

/* -------------------------------------------------------------- directorios */

ssb_res ssb_abs_path(const char *path, char *out, uint32_t size)
{
    if (path == NULL || out == NULL || size == 0)
        return ssb_err_arg;
#if defined(_WIN32)
    if (GetFullPathNameA(path, (DWORD)size, out, NULL) == 0)
    {
        snprintf(out, size, "%s", path);
        return ssb_err_io;
    }
    return ssb_ok;
#else
    if (path[0] == '/')
    {
        snprintf(out, size, "%s", path);
        return ssb_ok;
    }
    {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) == NULL)
        {
            snprintf(out, size, "%s", path);
            return ssb_err_io;
        }
        snprintf(out, size, "%s/%s", cwd, path);
    }
    return ssb_ok;
#endif
}

ssb_res ssb_config_path(const char *app, const char *file, char *out, uint32_t size)
{
    char base[700];

    if (app == NULL || file == NULL || out == NULL || size == 0)
        return ssb_err_arg;
#if defined(_WIN32)
    {
        const char *appdata = getenv("APPDATA");
        if (appdata == NULL || appdata[0] == 0)
            return ssb_err_io;
        snprintf(base, sizeof(base), "%s\\%s", appdata, app);
    }
#else
    {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        if (xdg != NULL && xdg[0] != 0)
        {
            snprintf(base, sizeof(base), "%s/%s", xdg, app);
        }
        else
        {
            const char *home = getenv("HOME");
            if (home == NULL || home[0] == 0)
                return ssb_err_io;
            snprintf(base, sizeof(base), "%s/.config/%s", home, app);
        }
    }
#endif
    ssb_mkdir(base);
#if defined(_WIN32)
    snprintf(out, size, "%s\\%s", base, file);
#else
    snprintf(out, size, "%s/%s", base, file);
#endif
    return ssb_ok;
}

ssb_res ssb_set_cwd(const char *path)
{
    if (path == NULL)
        return ssb_err_arg;
#if defined(_WIN32)
    return SetCurrentDirectoryA(path) ? ssb_ok : ssb_err_io;
#else
    return (chdir(path) == 0) ? ssb_ok : ssb_err_io;
#endif
}

ssb_res ssb_mkdir(const char *path)
{
    if (path == NULL || path[0] == 0)
        return ssb_err_arg;
#if defined(_WIN32)
    if (_mkdir(path) == 0)
        return ssb_ok;
#else
    if (mkdir(path, 0775) == 0)
        return ssb_ok;
#endif
    /* Que ya exista no es un fallo. */
    {
#if defined(_WIN32)
        DWORD a = GetFileAttributesA(path);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0)
            return ssb_ok;
#else
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
            return ssb_ok;
#endif
    }
    return ssb_err_io;
}

/* -------------------------------------------------------------------- mutex */

struct ssb_mutex_t
{
#if defined(_WIN32)
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t m;
#endif
};

ssb_mutex *ssb_mutex_create(void)
{
    ssb_mutex *m = (ssb_mutex *)calloc(1, sizeof(ssb_mutex));
    if (m == NULL)
        return NULL;
#if defined(_WIN32)
    InitializeCriticalSection(&m->cs);
#else
    pthread_mutex_init(&m->m, NULL);
#endif
    return m;
}

void ssb_mutex_destroy(ssb_mutex **m)
{
    if (m == NULL || *m == NULL)
        return;
#if defined(_WIN32)
    DeleteCriticalSection(&(*m)->cs);
#else
    pthread_mutex_destroy(&(*m)->m);
#endif
    free(*m);
    *m = NULL;
}

void ssb_mutex_lock(ssb_mutex *m)
{
    if (m == NULL)
        return;
#if defined(_WIN32)
    EnterCriticalSection(&m->cs);
#else
    pthread_mutex_lock(&m->m);
#endif
}

void ssb_mutex_unlock(ssb_mutex *m)
{
    if (m == NULL)
        return;
#if defined(_WIN32)
    LeaveCriticalSection(&m->cs);
#else
    pthread_mutex_unlock(&m->m);
#endif
}

/* -------------------------------------------------------------------- hilos */

struct ssb_thread_t
{
    ssb_thread_fn fn;
    void *ctx;
#if defined(_WIN32)
    HANDLE h;
#else
    pthread_t h;
#endif
};

#if defined(_WIN32)
static DWORD WINAPI i_thread_main(LPVOID arg)
{
    ssb_thread *t = (ssb_thread *)arg;
    t->fn(t->ctx);
    return 0;
}
#else
static void *i_thread_main(void *arg)
{
    ssb_thread *t = (ssb_thread *)arg;
    t->fn(t->ctx);
    return NULL;
}
#endif

ssb_thread *ssb_thread_start(ssb_thread_fn fn, void *ctx)
{
    ssb_thread *t;
    if (fn == NULL)
        return NULL;
    t = (ssb_thread *)calloc(1, sizeof(ssb_thread));
    if (t == NULL)
        return NULL;
    t->fn = fn;
    t->ctx = ctx;
#if defined(_WIN32)
    t->h = CreateThread(NULL, 0, i_thread_main, t, 0, NULL);
    if (t->h == NULL)
    {
        free(t);
        return NULL;
    }
#else
    if (pthread_create(&t->h, NULL, i_thread_main, t) != 0)
    {
        free(t);
        return NULL;
    }
#endif
    return t;
}

void ssb_thread_join(ssb_thread **t)
{
    if (t == NULL || *t == NULL)
        return;
#if defined(_WIN32)
    WaitForSingleObject((*t)->h, INFINITE);
    CloseHandle((*t)->h);
#else
    pthread_join((*t)->h, NULL);
#endif
    free(*t);
    *t = NULL;
}

#if !defined(_WIN32)

/* Sin Media Foundation no hay codificador del sistema. Se declara aqui para que
   el motor compile y la interfaz pueda ofrecer solo WAV. */
const char *ssb_format_ext(ssb_format fmt)
{
    switch (fmt)
    {
    case ssb_fmt_mp3:
        return "mp3";
    case ssb_fmt_m4a:
        return "m4a";
    default:
        return "wav";
    }
}

ssb_res ssb_encode(const char *wav_path, const char *out_path,
                   ssb_format fmt, uint32_t target_kbps)
{
    (void)wav_path;
    (void)out_path;
    (void)fmt;
    (void)target_kbps;
    return ssb_err_platform;
}

#endif

void ssb_sleep(uint32_t ms)
{
    ssb_sleep_ms(ms);
}

void ssb_sleep_ms(uint32_t ms)
{
#if defined(_WIN32)
    Sleep(ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}
