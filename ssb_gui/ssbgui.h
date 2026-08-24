/* Interfaz de SystemSoundBuffer. Consumidora de ssb_core: no toca WASAPI ni
   ficheros del buffer, solo pide picos y manda guardar. */
#ifndef SSBGUI_H
#define SSBGUI_H

#include <nappgui.h>
#include <stdarg.h>
#include "ssb.h"
#include "ssbtext.h"

/* Rayas separadoras de la barra: 4 en la fila de arriba y 4 en la de abajo. */
#define SEP_N 8

#define GUI_MAX_TRACKS 8
#define GUI_MAX_SOURCES 128

/* Colores del lienzo, resueltos con gui_alt_color() segun el tema del sistema.
   Se recalculan al arrancar y cada vez que el sistema cambia de tema. */
typedef struct _palette_t
{
    color_t back;
    color_t lane_a;
    color_t lane_b;
    color_t axis;
    color_t empty;
    color_t ruler_bg;
    color_t ruler_line;
    color_t ruler_text;
    color_t hud_bg;
    color_t hud_text;
    color_t hud_dim;
    color_t sel_fill;
    color_t sel_edge;
    color_t warn;
    color_t ok;
    color_t meter_bg;
    color_t chrome;    /* fondo de la ventana, detras de los controles */
    color_t chrome_tx; /* texto de las etiquetas de la barra */
    color_t sep;       /* la raya fina entre grupos de la barra */
} Palette;

typedef struct _gtrack_t
{
    ssb_track *track;
    ssb_source src;
    char_t name[64];
    color_t color;
    int muted;     /* silenciada por el usuario: no se exporta */
    int sys_muted; /* el endpoint del sistema esta mudo: grabaria silencio */
    int gone;      /* la fuente ya no esta en el sistema: se apago o se cerro */
} GTrack;

#define CMD_LINES 7

typedef struct _app_t
{
    Window *window;
    Layout *root;
    Layout *bar0;
    Layout *bar1;
    View *view;
    PopUp *sources;
    PopUp *buffer;
    PopUp *compress;
    PopUp *export_fmt;
    Label *lbl_buffer;
    Label *lbl_save;
    Label *lbl_export;
    Button *btn_add;
    Button *btn_pause;
    Button *btn_input;
    Button *btn_lang;
    Button *btn_live;
    Button *btn_all;
    Button *btn_save;
    Button *btn_zin;
    Button *btn_zout;
    Button *btn_folder;
    Button *btn_folder_def;
    Button *btn_small;
    Button *btn_cmd;
    Button *btn_play;
    Button *btn_mix;
    /* Las rayas finas que separan los grupos de la barra. Con los botones
       planos, que se funden con el fondo, la separacion en blanco ya no basta
       para leer los grupos de un vistazo.
       Son View y no Label: una etiqueta vacia no baja de unos 3 px de ancho y
       la raya salia como un bloque. Un View mide lo que se le dice. */
    View *sep[SEP_N];
    Edit *cmd;
    Layout *bar;      /* las dos filas, para poder ocultar la segunda */

    /* El estado se dibuja dentro del lienzo, no en controles nativos: asi nada
       de lo importante depende de que se vea el borde inferior de la ventana. */
    char_t status[220];
    char_t message[220];
    double message_at;
    uint32_t save_seq;
    uint32_t track_seq; /* nunca se reutiliza: dos anillos no pueden compartir carpeta */

    ssb_source list[GUI_MAX_SOURCES];
    uint32_t nsources;
    uint64_t src_version; /* de la foto del vigilante: cuando cambia, se rehace */

    GTrack tracks[GUI_MAX_TRACKS];
    uint32_t ntracks;

    Palette pal;
    /* 0 = el del sistema, 1 = claro forzado, 2 = oscuro forzado. */
    uint32_t theme;

    /* Ventana de tiempo visible. Nunca se sale de lo que hay en el buffer:
       `buffer_secs` es su tope y `live` el instante del ultimo dato. */
    double span_secs;
    double buffer_secs;   /* SOLO el valor por omision para pistas nuevas;
                             el ancho de la vista se pregunta al motor */
    ssb_time win_to;
    ssb_time live;
    int follow;
    int frozen;
    int recording;
    int compact;      /* modo reducido: lo minimo para grabar.
                         OJO: no llamarlo `small`, que Windows lo define
                         como macro en rpcndr.h y rompe la compilacion. */
    int cmdmode;      /* consola de comandos visible */
    int keyhelp;      /* F1: la chuleta de atajos, encima de la onda */
    char_t cmdout[CMD_LINES][200]; /* nada se graba hasta que se pulsa Grabar */

    /* Guion de comandos (--script fichero). Existe para las pruebas: manejar la
       aplicacion con pulsaciones simuladas pierde teclas cuando la ventana aun
       no tiene el foco de verdad, y eso ya nos ha hecho perseguir cuatro fallos
       que no existian. Un guion se ejecuta desde dentro y no depende del foco. */
    char_t script[96][200];
    uint32_t script_n;
    uint32_t script_i;
    double script_until;  /* mientras ctime < esto, el guion espera */
    char_t script_log[420]; /* eco de la consola a fichero: las capturas de
                               pantalla dependen de que ninguna otra ventana
                               este encima, y eso ya nos ha mentido */

    uint32_t lang;
    real32_t content_w;
    real32_t content_h;
    int ctrl_down; /* cacheado del ultimo movimiento del raton */
    /* Posicion del raton dentro del lienzo, para resaltar lo que hay debajo.
       Un boton dibujado que no reacciona al pasar por encima no parece
       pulsable, y ese es medio motivo por el que los propios se ven peor. */
    real32_t hover_x;
    real32_t hover_y;
    int zoomed; /* el usuario ha tocado el zoom: se respeta su ventana */

    /* Seleccion, en tiempo absoluto. */
    int selecting;
    int has_sel;
    ssb_time sel_a;
    ssb_time sel_b;

    /* Arrastre con el boton derecho para desplazar. */
    int panning;
    real32_t pan_x;

    ssb_peak *peaks;
    uint32_t peaks_cap;

    /* Iconos de transporte, generados por codigo (ssbicon.c). Un boton nativo
       no deja tocar el color del texto, pero si aceptar una imagen. */
    Image *ico_rec;
    Image *ico_stop;
    Image *ico_play;
    Image *ico_pause;

    Font *font;
    char_t dir[400];      /* buffers en disco */
    char_t savedir[400];  /* donde va lo que se guarda */
    int mix_export;       /* TRUE: todas las pistas activas en un solo fichero */

    /* Reproduccion de lo seleccionado. Se reproduce el fichero YA exportado, no
       una segunda lectura del anillo: asi lo que se oye es lo que se guardaria. */
    ssb_play *play;
    char_t play_file[620];
    /* Instante del buffer donde EMPIEZA lo que se esta reproduciendo.
       La cabeza se dibuja respecto a esto y no a la seleccion viva: la
       seleccion cambia al hacer clic, y entonces la cabeza daba saltos que no
       tenian nada que ver con lo que sonaba. */
    ssb_time play_from;
    /* Para poder distinguir un clic seco de un arrastre y devolver la seleccion
       si resulto ser lo primero. */
    int pending_seek;
    int had_sel;
    ssb_time sel_before_a;
    ssb_time sel_before_b;
    /* Coste de pintar el lienzo. Lo consulta la orden `perf`. */
    double draw_ms_sum;
    double draw_ms_max;
    uint32_t draw_frames;

    double label_clock;
    double mute_clock;
} App;

/* Iconos de transporte. */
void icons_create(App *app);
void icons_destroy(App *app);

/* La vista de ondas. */
View *wave_create(App *app);
void wave_resize(App *app);
void wave_clamp(App *app);

/* Duracion del buffer segun el MOTOR, no segun la interfaz. */
double app_buffer_secs(App *app);
void wave_palette(App *app);
color_t wave_track_color(const App *app, uint32_t index);
void wave_time_to_x(const App *app, ssb_time t, real32_t width, real32_t *x);
ssb_time wave_x_to_time(const App *app, real32_t x, real32_t width);

/* Tramo comun a todas las pistas; FALSE si aun no hay ninguna con datos. */
bool_t app_span(App *app, ssb_time *from, ssb_time *to);

/* TRUE mientras el circular no haya descartado nada, o sea mientras el buffer
   sigue creciendo. En cuanto descarta, esta en regimen y la onda se traslada. */
bool_t app_filling(App *app);

/* Aplica el tema a la ventana: fondo de los layouts y barra de titulo. */
void app_apply_chrome(App *app);

/* Vuelca los ajustes del usuario a disco. Se llama en cuanto cambia algo que
   debe sobrevivir al cierre — la carpeta, el formato, el buffer — y no solo al
   salir: un cierre forzoso no debe costarle al usuario volver a elegirlo todo.
   La comparten los botones y las ordenes de teclado, como todo lo demas. */
void app_save_settings(App *app);

/* Cambia el buffer de todas las pistas y deja el desplegable en su sitio. */
void app_set_buffer(App *app, uint32_t secs);

/* Texto en el idioma activo. */
const char_t *T(const App *app, ssb_txt id);

/* Vuelve a rotular todos los controles tras cambiar de idioma. */
void app_relabel(App *app);

/* Silencia o activa una pista. Silenciada no se exporta. */
void app_toggle_mute(App *app, uint32_t index);

/* Acciones compartidas por los botones y por los comandos: un solo camino. */
void app_set_recording(App *app, int recording);
bool_t app_add_track(App *app, const ssb_source *src);
void app_select_all(App *app);
void app_select_last(App *app, double secs);
void app_quick_save(App *app);
void app_reload_sources(App *app);
void app_set_small(App *app, int compact);
void app_set_cmdmode(App *app, int on);

/* Reproduce el tramo seleccionado, o pausa si ya esta sonando. */
void app_play_toggle(App *app);
void app_play_stop(App *app);

/* La consola de comandos. */
void cmd_run(App *app, const char_t *line);
void cmd_print(App *app, const char_t *fmt, ...);

/* Cierra una pista concreta, no solo la ultima. */
void app_close_track(App *app, uint32_t index);

/* Zoom por factor: <1 acerca, >1 aleja. */
void wave_zoom(App *app, double factor);

#endif /* SSBGUI_H */
