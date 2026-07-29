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
    //
    // The game has no way to take a command back out of its table, so a name handed to it
    // stays there for the life of the process pointing at AzForge's dispatch. That is what
    // shapes the teardown: dropping a mod drops its entry from the routing map and the
    // command then reports itself as gone, rather than the entry being removed and the game
    // left with a pointer to nothing.
    class ConsoleApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodConsoleApi*                                Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
