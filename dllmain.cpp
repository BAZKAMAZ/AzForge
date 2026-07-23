#include "pch.h"

#include "src/Core/ForgeBoot.h"

// DllMain only records the module handle so that this module's initialisation stays
// trivial while the loader lock is held. The loader's actual work runs from AzForge_Boot,
// which the winmm proxy calls explicitly.
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(lpReserved);

    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        Forge::Boot::SetModuleHandle(hModule);
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }

    return TRUE;
}
