/* Lee y ajusta volumenes: el maestro del dispositivo y el de cada sesion.
 *
 *     vol                 muestra todo
 *     vol master 67       pone el maestro al 67%
 *     vol app 6936 100    pone al 100% la sesion de ese pid
 *
 * Existe porque una sonda anterior cambiaba el volumen maestro para medir y,
 * si moria a mitad, lo dejaba donde estuviera. Una herramienta que toca estado
 * global de la maquina tiene que venir con su contraria.
 */
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ole32.lib")

static void w2a(const wchar_t *w, char *out, size_t cap)
{
    if (w == NULL) { if (cap) out[0] = 0; return; }
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL);
    out[cap - 1] = 0;
}

static void proc_name(DWORD pid, char *out, size_t cap)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    out[0] = 0;
    if (h == NULL) { snprintf(out, cap, "(pid %u)", (unsigned)pid); return; }
    {
        wchar_t buf[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, buf, &n))
        {
            wchar_t *s = wcsrchr(buf, L'\\');
            w2a(s ? s + 1 : buf, out, cap);
        }
    }
    CloseHandle(h);
}

int main(int argc, char **argv)
{
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioEndpointVolume *ep = NULL;
    IAudioSessionManager2 *mgr = NULL;
    IAudioSessionEnumerator *se = NULL;
    char devname[256] = "";
    float master = -1.0f;
    BOOL muted = FALSE;
    int count = 0, j;
    int rc = 1;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void **)&en)))
        return 1;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)))
        goto done;
    {
        IPropertyStore *ps = NULL;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps)))
        {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
                w2a(v.pwszVal, devname, sizeof(devname));
            PropVariantClear(&v);
            ps->Release();
        }
    }
    if (FAILED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void **)&ep)))
        goto done;

    if (argc >= 3 && strcmp(argv[1], "master") == 0)
    {
        float v = (float)(atof(argv[2]) / 100.0);
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        ep->SetMasterVolumeLevelScalar(v, NULL);
        ep->SetMute(FALSE, NULL);
        printf("maestro puesto al %.0f%%\n", v * 100.0);
    }

    ep->GetMasterVolumeLevelScalar(&master);
    ep->GetMute(&muted);
    printf("SALIDA: %s\n", devname);
    printf("  maestro: %.0f%%%s\n\n", master * 100.0, muted ? "   [MUDO]" : "");

    if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void **)&mgr)))
        goto done;
    if (FAILED(mgr->GetSessionEnumerator(&se)) || FAILED(se->GetCount(&count)))
        goto done;

    printf("SESIONES (volumen propio de cada aplicacion):\n");
    for (j = 0; j < count; ++j)
    {
        IAudioSessionControl *sc = NULL;
        IAudioSessionControl2 *sc2 = NULL;
        ISimpleAudioVolume *sv = NULL;
        DWORD pid = 0;
        float v = -1.0f;
        BOOL m = FALSE;
        AudioSessionState st = AudioSessionStateInactive;
        char nm[256];

        if (FAILED(se->GetSession(j, &sc)))
            continue;
        if (SUCCEEDED(sc->QueryInterface(__uuidof(IAudioSessionControl2), (void **)&sc2)))
        {
            sc2->GetProcessId(&pid);
            sc2->Release();
        }
        sc->GetState(&st);
        if (SUCCEEDED(sc->QueryInterface(__uuidof(ISimpleAudioVolume), (void **)&sv)))
        {
            sv->GetMasterVolume(&v);
            sv->GetMute(&m);
            if (argc >= 4 && strcmp(argv[1], "app") == 0 && pid == (DWORD)atoi(argv[2]))
            {
                float nv = (float)(atof(argv[3]) / 100.0);
                if (nv < 0.0f) nv = 0.0f;
                if (nv > 1.0f) nv = 1.0f;
                sv->SetMasterVolume(nv, NULL);
                sv->SetMute(FALSE, NULL);
                sv->GetMasterVolume(&v);
                m = FALSE;
                printf("  -> ajustada la sesion %u al %.0f%%\n", (unsigned)pid, v * 100.0);
            }
            sv->Release();
        }
        if (pid != 0)
        {
            proc_name(pid, nm, sizeof(nm));
            printf("  %-26s pid %-6u  volumen %3.0f%%%s  %s\n", nm, (unsigned)pid,
                   v * 100.0, m ? " [MUDO]" : "",
                   st == AudioSessionStateActive ? "SUENA" : "callada");
        }
        sc->Release();
    }
    rc = 0;

done:
    if (se != NULL) se->Release();
    if (mgr != NULL) mgr->Release();
    if (ep != NULL) ep->Release();
    if (dev != NULL) dev->Release();
    if (en != NULL) en->Release();
    CoUninitialize();
    return rc;
}
