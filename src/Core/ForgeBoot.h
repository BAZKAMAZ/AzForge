#pragma once

#include "src/Mods/ModRegistry.h"

namespace Forge
{
    // Entry point of the loader. The proxy calls AzForge_Boot from the first winmm export the
    // process forwards, not from its DllMain, so none of this work - archives, inflate,
    // signature scans, detours - happens under the loader lock.
    class Boot
    {
    public:
        static void                                                 SetModuleHandle(HMODULE module);

        // True when the loader is up and mods are running. False means the game is not the
        // one AzForge was built for, or the log could not be opened - in both cases the
        // process is left exactly as it was, which is why the proxy ignores the answer.
        static bool                                                 Run();
        static void                                                 Stop();

        static const ModRegistry&                                   Registry();
    };
}

extern "C" __declspec(dllexport) void __cdecl AzForge_Boot();
extern "C" __declspec(dllexport) void __cdecl AzForge_Shutdown();
