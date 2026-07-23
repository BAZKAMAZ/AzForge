#pragma once

#include "src/Mods/ModRegistry.h"

namespace Forge
{
    // Entry point of the loader. The proxy calls AzForge_Boot once the process is up; the
    // work itself lives here so that DllMain stays free of anything that can block.
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
