/* Compara las dos lineas de tiempo que WASAPI ofrece en cada paquete:
 *
 *   qpcPosition   estimacion de reloj de pared, en unidades de 100 ns
 *   devPosition   posicion de muestra del dispositivo, en frames
 *
 * Medido en esta maquina: el qpc oscila +-4 ms de un paquete a otro. Si la
 * posicion de dispositivo avanza exactamente los frames entregados, entonces NO
 * se pierde audio y todo hueco deducido del qpc es un artefacto del reloj.
 *
 * Eso es lo que decide como hay que colocar los bloques.
 *
 *     devpos [segundos]
 */
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdio.h>

#pragma comment(lib, "ole32.lib")

int main(int argc, char **argv)
{
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioClient *ac = NULL;
    IAudioCaptureClient *cap = NULL;
    WAVEFORMATEX *wf = NULL;
    double secs = (argc > 1) ? atof(argv[1]) : 20.0;
    UINT64 prev_dev = 0, prev_qpc = 0;
    UINT32 prev_frames = 0;
    int first = 1;
    long total = 0, huecos_dev = 0, solapes_dev = 0, flag_ts = 0, flag_disc = 0, flag_sil = 0;
    double qpc_min = 1e9, qpc_max = -1e9;
    UINT64 dev_lost = 0;
    DWORD t0;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void **)&en)))
        return 1;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)))
        return 1;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&ac)))
        return 1;
    ac->GetMixFormat(&wf);
    if (FAILED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                              10000000, 0, wf, NULL)))
        return 1;
    if (FAILED(ac->GetService(__uuidof(IAudioCaptureClient), (void **)&cap)))
        return 1;
    ac->Start();

    printf("dispositivo a %u Hz, %u canales\n", wf->nSamplesPerSec, wf->nChannels);
    printf("paq  frames  dev_delta-frames   qpc_delta_ms  esperado_ms   error_ms  flags\n");

    t0 = GetTickCount();
    while ((GetTickCount() - t0) < (DWORD)(secs * 1000.0))
    {
        UINT32 packet = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet > 0)
        {
            BYTE *data = NULL;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 devpos = 0, qpc = 0;
            if (FAILED(cap->GetBuffer(&data, &frames, &flags, &devpos, &qpc)))
                break;
            if (frames > 0)
            {
                if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) flag_ts++;
                if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) flag_disc++;
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) flag_sil++;
                if (!first)
                {
                    /* Cuanto avanzo la posicion del dispositivo, frente a los
                       frames que nos entregaron en el paquete anterior. */
                    long long ddev = (long long)(devpos - prev_dev) - (long long)prev_frames;
                    double dqpc = (double)(qpc - prev_qpc) / 10000.0;
                    double esperado = 1000.0 * (double)prev_frames / (double)wf->nSamplesPerSec;
                    double err = dqpc - esperado;
                    if (ddev > 0) { huecos_dev++; dev_lost += (UINT64)ddev; }
                    if (ddev < 0) solapes_dev++;
                    if (err < qpc_min) qpc_min = err;
                    if (err > qpc_max) qpc_max = err;
                    if (total < 12 || ddev != 0 || err > 4.0 || err < -4.0)
                        printf("%4ld %7u %12lld %14.3f %12.3f %10.3f  %s%s%s\n",
                               total, frames, ddev, dqpc, esperado, err,
                               (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) ? "TS " : "",
                               (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ? "DISC " : "",
                               (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? "SIL" : "");
                }
                first = 0;
                prev_dev = devpos;
                prev_qpc = qpc;
                prev_frames = frames;
                total++;
            }
            cap->ReleaseBuffer(frames);
            packet = 0;
        }
        Sleep(5);
    }
    ac->Stop();

    printf("\n=== resumen de %ld paquetes ===\n", total);
    printf("  posicion de dispositivo: %ld huecos, %ld solapes, %llu frames perdidos en total (%.2f ms)\n",
           huecos_dev, solapes_dev, (unsigned long long)dev_lost,
           1000.0 * (double)dev_lost / (double)wf->nSamplesPerSec);
    printf("  error del qpc frente a los frames entregados: min %.3f ms, max %.3f ms\n", qpc_min, qpc_max);
    printf("  banderas: TIMESTAMP_ERROR %ld, DATA_DISCONTINUITY %ld, SILENT %ld\n",
           flag_ts, flag_disc, flag_sil);
    return 0;
}
