/* Textos de la interfaz en dos idiomas.
 *
 * Tabla plana y sin dependencias: el idioma es un indice y cada cadena un
 * identificador. NAppGUI trae su propio sistema de recursos (nrc), pero para dos
 * idiomas y cuarenta cadenas eso es mas maquinaria que la que hace falta, y esto
 * se puede cambiar en caliente sin recargar nada.
 */
#ifndef SSBTEXT_H
#define SSBTEXT_H

typedef enum
{
    TXT_ADD = 0,
    TXT_REMOVE,
    TXT_REC,
    TXT_STOP_REC,
    TXT_FREEZE_VIEW,
    TXT_RESUME_VIEW,
    TXT_LIVE,
    TXT_SELECT_ALL,
    TXT_SAVE,
    TXT_BUFFER,
    TXT_FORMAT,
    TXT_EXPORT,
    TXT_LOSSLESS,
    TXT_UNCOMPRESSED,
    TXT_LOSSLESS24,
    TXT_UNCOMPRESSED24,
    TXT_ZOOM_IN,
    TXT_ZOOM_OUT,
    TXT_FOLDER,
    TXT_FOLDER_RESET,
    TXT_MODE_SMALL,
    TXT_MODE_FULL,
    TXT_MODE_CMD,
    TXT_MODE_NOCMD,
    TXT_CMD_HELLO,
    TXT_LANG_BUTTON,
    TXT_TITLE,

    TXT_HINT,
    TXT_NO_TRACKS,
    TXT_STATUS,
    TXT_SELECTION,
    TXT_BANNER_RECORDING,
    TXT_BANNER_READY,
    TXT_BANNER_STOPPED,
    TXT_BANNER_FROZEN,
    TXT_MUTED,
    TXT_SYS_MUTED,
    TXT_NO_SIGNAL,
    TXT_SRC_GONE,
    TXT_KEYS_TITLE,
    TXT_KEY_00,
    TXT_KEY_01,
    TXT_KEY_02,
    TXT_KEY_03,
    TXT_KEY_04,
    TXT_KEY_05,
    TXT_KEY_06,
    TXT_KEY_07,
    TXT_KEY_08,
    TXT_KEY_09,
    TXT_KEY_10,
    TXT_KEY_11,

    TXT_MSG_STOPPED,
    TXT_MSG_RESUMED,
    TXT_MSG_NO_SELECTION,
    TXT_MSG_SAVED,
    TXT_MSG_MIXED,
    TXT_MSG_PLAY_FAILED,
    TXT_PLAY,
    TXT_PAUSE,
    TXT_RESUME_PLAY,
    TXT_MIX_ON,
    TXT_MIX_OFF,
    TXT_MSG_ENCODE_FAILED,
    TXT_MSG_WORKING,
    TXT_MSG_BUSY,
    TXT_MSG_MIX_RATE,
    TXT_MSG_MIX_FAILED,
    TXT_MSG_OUT_OF_RANGE,
    TXT_MSG_OPEN_FAILED,
    TXT_MSG_ALL_MUTED,
    TXT_MSG_MUTED,
    TXT_MSG_UNMUTED,
    TXT_MSG_FOLDER,
    TXT_MSG_BUFFER_CHANGED,
    TXT_SAVE_DIALOG,

    TXT_TIP_STOP_INPUT,
    TXT_TIP_FREEZE_VIEW,
    TXT_TIP_BUFFER,
    TXT_TIP_FORMAT,
    TXT_TIP_EXPORT,
    TXT_TIP_LANG,
    TXT_TIP_FOLDER,

    TXT_COUNT
} ssb_txt;

#define LANG_EN 0
#define LANG_ES 1
#define LANG_COUNT 2

/* La tabla vive en ssbtext.c y aqui solo se declara.
   Definirla `static` en la cabecera daba una copia por cada .c que la incluye, y
   gcc con -Wunused-variable rechaza las copias que no se usan: la interfaz no
   compilaba en Linux por eso. */
extern const char_t *const SSB_TEXT[LANG_COUNT][TXT_COUNT];

#endif /* SSBTEXT_H */
