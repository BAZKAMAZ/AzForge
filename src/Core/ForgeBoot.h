#pragma once

#include <windows.h>

#include "src/Mods/ModRegistry.h"

namespace Forge
{
    class Boot
    {
    public:
        static void                                                 SetModuleHandle(HMODULE module);
        static bool                                                 Run();
        static void                                                 Stop();

        // What must outlive the game's own shutdown: the crash handler and the log
        static void                                                 Finish();

        static const ModRegistry&                                   Registry();
    };
}

extern "C" __declspec(dllexport) void __cdecl AzForge_Boot();
extern "C" __declspec(dllexport) void __cdecl AzForge_Shutdown();
