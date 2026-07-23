#pragma once

#include <LauncherApi.h>

namespace Forge
{
    struct ModContext;

    // Loader side of the launcher interface.
    //
    // Three detours carry it: Launcher_OnInitDialog @0x0040c7e0 runs the original first and
    // then reports the dialog as open, so a mod sees it already styled the way the game
    // wants it; Launcher_OnCommand @0x0040c5d0 reports the command before the original and
    // skips it when a mod claims it; Dialog_RunModal @0x00424520 reports the close once the
    // modal loop has returned, which is the only place that catches every way out of the
    // dialog, including the window's close button.
    class LauncherApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodLauncherApi*                               Table();

        // Drops every subscription belonging to one mod. Called while the mod is being
        // unloaded, so a callback can no longer be reached.
        static void                                                 ForgetMod(ModContext* owner);
    };
}
