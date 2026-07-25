#pragma once

#include <ConsoleApi.h>

namespace Forge
{
    struct ModContext;

    // Loader side of the developer console.
    //
    // It installs no detours: it calls the game's console routines and registers one dispatch
    // handler with the game's command table, which routes a typed command to whichever mod
    // added it. Available only when the console routines were located.
    class ConsoleApi
    {
    public:
        static bool                                                 Install();

        static const AmodConsoleApi*                                Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
