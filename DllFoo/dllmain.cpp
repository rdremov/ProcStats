#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include "cstdio"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        printf("DllFoo DLL_PROCESS_ATTACH\n");
        break;
    case DLL_THREAD_ATTACH:
        printf("DllFoo DLL_THREAD_ATTACH\n");
        break;
    case DLL_THREAD_DETACH:
        printf("DllFoo DLL_THREAD_DETACH\n");
        break;
    case DLL_PROCESS_DETACH:
        printf("DllFoo DLL_PROCESS_DETACH\n");
        break;
    }
    return TRUE;
}

