/* Decodifica a WAV de 16 bits cualquier fichero que Media Foundation sepa leer.
 *
 * Existe porque hasta ahora la exportacion comprimida solo se validaba por la
 * cabecera: "192 kbps, 48 kHz, 555 tramas" no dice nada de las MUESTRAS. Un MP3
 * con la cabecera perfecta puede llevar silencio, chasquidos o el audio de otra
 * cosa. Sin decodificar no se esta comprobando el contenido, se esta creyendo.
 *
 *     mfdecode entrada.mp3 salida.wav
 */
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

static void put32(FILE *f, unsigned int v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, unsigned short v) { fwrite(&v, 2, 1, f); }

int main(int argc, char **argv)
{
    IMFSourceReader *rd = NULL;
    IMFMediaType *want = NULL;
    IMFMediaType *got = NULL;
    FILE *out = NULL;
    wchar_t win[1024];
    UINT32 rate = 0, ch = 0, bits = 0;
    unsigned int total = 0;
    int rc = 1;

    if (argc < 3)
    {
        fprintf(stderr, "uso: mfdecode entrada salida.wav\n");
        return 2;
    }
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, win, 1024);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE)))
    {
        fprintf(stderr, "MFStartup fallo\n");
        return 1;
    }
    if (FAILED(MFCreateSourceReaderFromURL(win, NULL, &rd)))
    {
        fprintf(stderr, "no puedo abrir %s\n", argv[1]);
        goto done;
    }

    /* Se pide PCM 16 bits y que MF ponga el decodificador que haga falta. */
    if (FAILED(MFCreateMediaType(&want)))
        goto done;
    want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    want->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (FAILED(rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, want)))
    {
        fprintf(stderr, "no puedo fijar PCM 16\n");
        goto done;
    }
    if (FAILED(rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &got)))
        goto done;
    got->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    got->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
    got->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
    if (rate == 0 || ch == 0 || bits != 16)
    {
        fprintf(stderr, "formato inesperado: %u Hz %u ch %u bits\n", rate, ch, bits);
        goto done;
    }

    out = fopen(argv[2], "wb");
    if (out == NULL)
    {
        fprintf(stderr, "no puedo escribir %s\n", argv[2]);
        goto done;
    }
    /* Cabecera con los tamanos a cero; se rellenan al final. */
    fwrite("RIFF", 1, 4, out); put32(out, 0); fwrite("WAVEfmt ", 1, 8, out);
    put32(out, 16); put16(out, 1); put16(out, (unsigned short)ch);
    put32(out, rate); put32(out, rate * ch * 2);
    put16(out, (unsigned short)(ch * 2)); put16(out, 16);
    fwrite("data", 1, 4, out); put32(out, 0);

    for (;;)
    {
        IMFSample *smp = NULL;
        IMFMediaBuffer *buf = NULL;
        DWORD flags = 0;
        BYTE *p = NULL;
        DWORD len = 0;

        if (FAILED(rd->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                  0, NULL, &flags, NULL, &smp)))
            break;
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
        {
            if (smp != NULL) smp->Release();
            break;
        }
        if (smp == NULL)
            continue;   /* hueco declarado por el lector: sigue habiendo stream */
        if (SUCCEEDED(smp->ConvertToContiguousBuffer(&buf)))
        {
            if (SUCCEEDED(buf->Lock(&p, NULL, &len)))
            {
                fwrite(p, 1, len, out);
                total += len;
                buf->Unlock();
            }
            buf->Release();
        }
        smp->Release();
    }

    fseek(out, 4, SEEK_SET);  put32(out, 36 + total);
    fseek(out, 40, SEEK_SET); put32(out, total);
    printf("%u Hz  %u ch  %u frames  %.3f s\n",
           rate, ch, total / (ch * 2), (double)(total / (ch * 2)) / (double)rate);
    rc = 0;

done:
    if (out != NULL) fclose(out);
    if (got != NULL) got->Release();
    if (want != NULL) want->Release();
    if (rd != NULL) rd->Release();
    MFShutdown();
    CoUninitialize();
    return rc;
}
