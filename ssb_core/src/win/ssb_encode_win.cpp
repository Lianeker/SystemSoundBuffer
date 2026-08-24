/* Exportacion a formatos comprimidos usando SOLO APIs del sistema.
 *
 * Media Foundation trae el codificador: en esta maquina MFTEnumEx sobre
 * MFT_CATEGORY_AUDIO_ENCODER lista nueve, entre ellos "MP3 Encoder ACM Wrapper
 * MFT" y "Microsoft AAC Audio Encoder MFT". Las unicas DLL que arrastra el
 * ejecutable son MFPlat, MF, MFReadWrite y ole32, todas de System32: cero
 * dependencias externas, que es la regla del proyecto.
 *
 * Se transcodifica desde el WAV ya escrito en vez de codificar directamente
 * desde el anillo. Es deliberado: asi el camino de exportacion (ventana comun
 * entre pistas, relleno de huecos, recorte exacto, pistas silenciadas) sigue
 * siendo uno solo y ya verificado, y esto es solo un paso mas al final.
 *
 * TRAMPA documentada, y por eso se comprueba el resultado: con
 * MFCreateMP3MediaSink, si no se fija el tipo en el IMFMediaTypeHandler del
 * stream sink, TODO devuelve S_OK y el fichero sale remuestreado en silencio a
 * 32 kHz. Aqui se usa el camino por URL, que no lo sufre, y aun asi se
 * reabre el fichero al final para confirmar la frecuencia.
 */

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "ssb.h"
}

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

static void i_to_wide(const char *utf8, wchar_t *out, int cap)
{
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap);
}

extern "C" const char *ssb_format_ext(ssb_format fmt)
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

/* Elige el tipo de salida que conserva frecuencia y canales y cuyo caudal se
   acerca mas al pedido. Con `target_kbps` a 0 se coge el mas alto. */
static IMFMediaType *i_pick_type(IMFCollection *coll, UINT32 rate, UINT32 ch, uint32_t target_kbps)
{
    IMFMediaType *best = NULL;
    UINT32 best_bps = 0;
    DWORD cnt = 0, i;
    UINT32 want = target_kbps * 125u; /* kbps -> bytes por segundo */

    if (FAILED(coll->GetElementCount(&cnt)))
        return NULL;
    for (i = 0; i < cnt; ++i)
    {
        IUnknown *unk = NULL;
        IMFMediaType *t = NULL;
        UINT32 r = 0, c = 0, bps = 0;
        if (FAILED(coll->GetElement(i, &unk)))
            continue;
        if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&t))))
        {
            t->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &r);
            t->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &c);
            t->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bps);
            if (r == rate && c == ch)
            {
                int better;
                if (best == NULL)
                    better = 1;
                else if (want == 0)
                    better = (bps > best_bps) ? 1 : 0;
                else
                {
                    UINT32 da = (bps > want) ? bps - want : want - bps;
                    UINT32 db = (best_bps > want) ? best_bps - want : want - best_bps;
                    better = (da < db) ? 1 : 0;
                }
                if (better)
                {
                    if (best != NULL)
                        best->Release();
                    best = t;
                    best->AddRef();
                    best_bps = bps;
                }
            }
            t->Release();
        }
        unk->Release();
    }
    return best;
}

/* Reabre el fichero producido y devuelve su frecuencia real. 0 si no se puede
   leer, que ya es motivo para no fiarse de el. */
static UINT32 i_output_rate(const wchar_t *path)
{
    IMFSourceReader *rd = NULL;
    IMFMediaType *mt = NULL;
    UINT32 rate = 0;
    if (FAILED(MFCreateSourceReaderFromURL(path, NULL, &rd)))
        return 0;
    if (SUCCEEDED(rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &mt)))
    {
        mt->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        mt->Release();
    }
    rd->Release();
    return rate;
}

extern "C" ssb_res ssb_encode(const char *wav_path, const char *out_path,
                              ssb_format fmt, uint32_t target_kbps)
{
    wchar_t win[1024], wout[1024];
    HRESULT hr = S_OK;
    IMFSourceReader *reader = NULL;
    IMFMediaType *pcmreq = NULL, *pcm = NULL, *enc = NULL;
    IMFSinkWriter *writer = NULL;
    IMFCollection *coll = NULL;
    DWORD sidx = 0;
    UINT32 rate = 0, ch = 0, outrate = 0;
    GUID target;
    int started = 0;
    int couninit = 0;
    ssb_res res = ssb_err_platform;

    if (wav_path == NULL || out_path == NULL)
        return ssb_err_arg;
    if (fmt == ssb_fmt_wav)
        return ssb_err_arg; /* no hay nada que transcodificar */

    i_to_wide(wav_path, win, 1024);
    i_to_wide(out_path, wout, 1024);
    target = (fmt == ssb_fmt_mp3) ? MFAudioFormat_MP3 : MFAudioFormat_AAC;

    /* El hilo puede estar ya en un apartamento distinto (la captura usa MTA).
       En ese caso CoInitializeEx devuelve RPC_E_CHANGED_MODE y NO inicializa:
       llamar a CoUninitialize despues desequilibraria la cuenta y tumbaria COM
       para el resto de la aplicacion. */
    couninit = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)) ? 1 : 0;
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE)))
    {
        if (couninit)
            CoUninitialize();
        return ssb_err_platform;
    }
    started = 1;

    /* 1. Lector forzado a entregar PCM de 16 bits. */
    hr = MFCreateSourceReaderFromURL(win, NULL, &reader);
    if (FAILED(hr))
        goto done;
    if (FAILED(MFCreateMediaType(&pcmreq)))
        goto done;
    pcmreq->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pcmreq->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pcmreq);
    if (FAILED(hr))
        goto done;
    hr = reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pcm);
    if (FAILED(hr))
        goto done;
    pcm->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    pcm->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);

    /* 2. De los tipos que el sistema puede producir, el que conserva la fuente. */
    hr = MFTranscodeGetAudioOutputAvailableTypes(target, MFT_ENUM_FLAG_ALL, NULL, &coll);
    if (FAILED(hr))
        goto done;
    enc = i_pick_type(coll, rate, ch, target_kbps);
    if (enc == NULL)
    {
        res = ssb_err_notfound; /* el sistema no sabe codificar a eso */
        goto done;
    }

    /* 3. El sink lo elige la extension del fichero de salida. */
    hr = MFCreateSinkWriterFromURL(wout, NULL, NULL, &writer);
    if (FAILED(hr))
        goto done;
    hr = writer->AddStream(enc, &sidx);
    if (FAILED(hr))
        goto done;
    hr = writer->SetInputMediaType(sidx, pcm, NULL);
    if (FAILED(hr))
        goto done;
    hr = writer->BeginWriting();
    if (FAILED(hr))
        goto done;

    /* 4. Bombear las muestras del lector al escritor. */
    for (;;)
    {
        DWORD flags = 0;
        LONGLONG ts = 0;
        IMFSample *s = NULL;
        hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &flags, &ts, &s);
        if (FAILED(hr))
            goto done;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            if (s != NULL)
                s->Release();
            break;
        }
        if (s == NULL)
            continue;
        hr = writer->WriteSample(sidx, s);
        s->Release();
        if (FAILED(hr))
            goto done;
    }
    hr = writer->Finalize();
    if (FAILED(hr))
        goto done;
    writer->Release();
    writer = NULL;

    /* 5. Comprobar que no nos han remuestreado por detras. */
    outrate = i_output_rate(wout);
    if (outrate == 0)
        res = ssb_err_io;
    else if (outrate != rate)
        res = ssb_err_platform; /* salio con otra frecuencia: no vale */
    else
        res = ssb_ok;

done:
    if (writer != NULL)
        writer->Release();
    if (coll != NULL)
        coll->Release();
    if (enc != NULL)
        enc->Release();
    if (pcm != NULL)
        pcm->Release();
    if (pcmreq != NULL)
        pcmreq->Release();
    if (reader != NULL)
        reader->Release();
    if (started)
        MFShutdown();
    if (couninit)
        CoUninitialize();
    if (res != ssb_ok && FAILED(hr) && res == ssb_err_platform)
        remove(out_path); /* no dejar un fichero a medias */
    return res;
}
