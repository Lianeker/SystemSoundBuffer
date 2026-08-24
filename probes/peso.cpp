/* ¿Cuanto pesa un control nativo frente a uno dibujado en el lienzo?
 *
 * Se mide, no se supone. Windows lleva la cuenta de los objetos USER y GDI que
 * consume un proceso, y cada control nativo es una ventana: un objeto USER, con
 * su sitio en la cola de mensajes. Un boton dibujado no es ningun objeto: son
 * cuatro llamadas de pintado y una comparacion de rectangulos al pulsar.
 *
 * Lo que esto NO mide es el coste de CPU de repintar, que va por otro lado (el
 * lienzo se repinta entero a 25 fps de todas formas). Para eso esta el tiempo
 * de dibujo que la propia aplicacion anota en su registro.
 *
 *     peso <nombre-o-pid>          una vez
 *     peso <nombre-o-pid> 20       vigila 20 s y avisa si algo crece
 */
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "psapi.lib")

static DWORD i_find(const char *name)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;
    DWORD pid = 0;
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe))
    {
        do
        {
            if (_stricmp(pe.szExeFile, name) == 0)
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static void i_report(HANDLE h, const char *tag)
{
    PROCESS_MEMORY_COUNTERS pmc;
    DWORD gdi = GetGuiResources(h, GR_GDIOBJECTS);
    DWORD usr = GetGuiResources(h, GR_USEROBJECTS);
    DWORD handles = 0;
    GetProcessHandleCount(h, &handles);
    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(h, &pmc, sizeof(pmc));
    printf("%-10s  USER %4lu   GDI %4lu   handles %5lu   memoria %6.1f MB\n",
           tag, (unsigned long)usr, (unsigned long)gdi, (unsigned long)handles,
           (double)pmc.WorkingSetSize / 1048576.0);
}

int main(int argc, char **argv)
{
    DWORD pid;
    HANDLE h;
    int secs = (argc > 2) ? atoi(argv[2]) : 0;

    if (argc < 2)
    {
        printf("uso: peso <nombre.exe|pid> [segundos de vigilancia]\n");
        return 2;
    }
    pid = (DWORD)atoi(argv[1]);
    if (pid == 0)
        pid = i_find(argv[1]);
    if (pid == 0)
    {
        printf("no encuentro el proceso %s\n", argv[1]);
        return 1;
    }
    h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (h == NULL)
    {
        printf("no puedo abrir el proceso %lu\n", (unsigned long)pid);
        return 1;
    }
    printf("proceso %lu\n", (unsigned long)pid);
    i_report(h, "inicio");
    if (secs > 0)
    {
        int i;
        for (i = 0; i < secs; i += 5)
        {
            char tag[16];
            Sleep(5000);
            snprintf(tag, sizeof(tag), "+%ds", i + 5);
            i_report(h, tag);
        }
    }
    CloseHandle(h);
    return 0;
}
