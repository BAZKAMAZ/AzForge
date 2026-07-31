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

        static const ModRegistry&                                   Registry();
    };
}

extern "C" __declspec(dllexport) void __cdecl AzForge_Boot();
extern "C" __declspec(dllexport) void __cdecl AzForge_Shutdown();
